//
// Created by ki608 on 2022-03-20.
//

#include "TimePlot.hpp"

#include "cpph/macros.hxx"
#include "cpph/utility/cleanup.hxx"
#include "imgui.h"
#include "imgui_extension.h"
#include "implot.h"

auto TimePlotWindowManager::CreateSlot(string name) -> TimePlotSlotProxy
{
    VerifyMainThread();

    auto data = make_shared<TimePlot::SlotData>();

    TimePlotSlotProxy proxy;
    proxy._body = data;

    data->name = std::move(name);
    data->pointsPendingUploaded.reserve(256);

    _slots.insert(std::move(data));
    return proxy;
}

void TimePlotWindowManager::TickWindow()
{
    using namespace ImGui;

    /*
     * 1. 메인 스레드에서 각 슬롯 프록시가 데이터 업로드 -> SlotData.pointsPendingUpload
     * 2. 각 프레임 윈도우 서술자 업데이트 (가시 영역). 변경 시 dirty 마크
     * 3. 주기적으로 메인 스레드에서 각 슬롯 iterate -> 타겟 윈도우가 dirty하거나, 업로드된 데이터 있으면
     *    async.targets에 해당 슬롯데이터 복사 및 async.allValues에 업로드 된 데이터 복사.
     * as.4. 각 슬롯의 allValues를 윈도우의 프레임 영역에 맞게, 밀도를 고려해 샘플링 후 cacheBuild에 넣는다.
     *       이후 각 슬롯의 axis range 값을 올바르게 설정.
     * as.5. 메인 스레드에 _fnMainThreadSwapBuffer 함수 퍼블리시.
     * 6. 메인 스레드에서 cache swap 수행.
     */
    /// Validate cache
    if (not _caching && _timerCacheTrig.check())
    {
        _fnTriggerAsyncJob();
    }

    /// Render MenuBar
    if (CondInvoke(BeginMainMenuBar(), &EndMainMenuBar))
    {
        if (CondInvoke(BeginMenu("View"), &EndMenu))
        {
            Separator();
            MenuItem("Time Plots", NULL, &_widget.bShowListPanel);
        }
    }

    /// Render Main Window

    if (not _widget.bShowListPanel)
        return;

    ImGui::SetNextWindowSize({640, 480}, ImGuiCond_Once);
    if (CPPH_CLEANUP(&End); Begin("Time Plot List", &_widget.bShowListPanel))
    {
        auto timeNow = steady_clock::now();

        for (auto& slot : _slots)
        {
            auto spinChars = "*|/-\\|/-"sv;
            bool bLatest = (timeNow - slot->timeLastUpload) < 1s;
            auto fnPopupID = [&] { return usprintf("##%p", slot.get()); };

            if (ImGui::Selectable(fnPopupID(), false, ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_SpanAllColumns))
            {
                OpenPopup(fnPopupID());
            }

            ImGui::PushStyleColor(ImGuiCol_Text, bLatest ? ColorRefs::FrontOkay : ColorRefs::BackOkay);
            ImGui::SameLine();
            ImGui::Text("[%c]", spinChars[slot->uploadSequence % spinChars.size()]);
            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, bLatest ? ColorRefs::Enabled : ColorRefs::Disabled);
            ImGui::SameLine();
            ImGui::TextUnformatted(slot->name.c_str());

            ImGui::PopStyleColor(2);

            if (CondInvoke(ImGui::BeginPopup(fnPopupID()), &EndPopup))
            {
            }
        }
    }
}

void TimePlotWindowManager::_fnAsyncValidateCache()
{
    // Perform caching

    // Request swap buffer on main thread.
    PostEventMainThread(bind(&TimePlotWindowManager::_fnMainThreadSwapBuffer, this));
}

void TimePlotWindowManager::_fnMainThreadSwapBuffer()
{
    VerifyMainThread();
    _caching = false;
}

void TimePlotWindowManager::_fnTriggerAsyncJob()
{
    VerifyMainThread();

    bool bHasAnyInvalidCache = false;
    _async.targets.clear();

    for (auto iter = _slots.begin(); iter != _slots.end();)
    {
        if ((**iter).bMarkDestroied)
        {
            iter = _slots.erase(iter);
        }
        else
        {
            auto& slot = *iter;
            CPPH_FINALLY(++iter);

            bool bCacheInvalid = false;
            bCacheInvalid |= not slot->pointsPendingUploaded.empty();

            auto refWindow = slot->targetWindow.lock();
            if (not refWindow) { continue; }  // Not being plotted.

            bCacheInvalid |= refWindow->bDirty;

            if (not bCacheInvalid) { continue; }
            bHasAnyInvalidCache = true;

            auto& async = slot->async;
            async.frameInfo = refWindow->frameInfo;
            async.allValues.enqueue_n(slot->pointsPendingUploaded.begin(), slot->pointsPendingUploaded.size());

            slot->pointsPendingUploaded.clear();
        }
    }

    // TODO: Iterate windows, clear dirty flag

    // Trigger async job
    if (bHasAnyInvalidCache)
    {
        _caching = true;
        _asyncWorker.post(bind(&TimePlotWindowManager::_fnAsyncValidateCache, this));
    }
}
