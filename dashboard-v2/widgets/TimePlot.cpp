//
// Created by ki608 on 2022-03-20.
//

#include "TimePlot.hpp"

#include "Application.hpp"
#include "cpph/helper/nlohmann_json_macros.hxx"
#include "cpph/macros.hxx"
#include "cpph/utility/cleanup.hxx"
#include "cpph/utility/random.hxx"
#include "imgui.h"
#include "imgui_extension.h"
#include "implot.h"
#include "perfkit/configs.h"

PERFKIT_DECLARE_SUBCATEGORY(GConfig::Widgets)
{
    struct PlotWindow
    {
        string key;
        string title;

        CPPHEADERS_DEFINE_NLOHMANN_JSON_ARCHIVER(PlotWindow, key, title);
    };

    PERFKIT_CONFIGURE(TimePlotWindows, vector<PlotWindow>{}).confirm();
}

TimePlotWindowManager::TimePlotWindowManager()
{
    Application::Get()->OnLoadWorkspace +=
            [this] {
                auto& list = GConfig::Widgets::TimePlotWindows.ref();

                for (auto& l : list)
                {
                    _createNewPlotWindow(l.key)->title = l.title;
                }

                _widget.bShowListPanel = RefPersistentNumber("TimePlotPersistant");
            };

    Application::Get()->OnDumpWorkspace +=
            [this] {
                vector<GConfig::Widgets::PlotWindow> wnds;
                wnds.reserve(_windows.size());

                for (auto& wnd : _windows)
                {
                    auto& elem = wnds.emplace_back();
                    elem.key = wnd->key;
                    elem.title = wnd->title;
                }

                GConfig::Widgets::TimePlotWindows.commit(wnds);
                RefPersistentNumber("TimePlotPersistant") = _widget.bShowListPanel;
            };
}

auto TimePlotWindowManager::CreateSlot(string name) -> TimePlotSlotProxy
{
    VerifyMainThread();

    auto data = make_shared<TimePlot::SlotData>();

    TimePlotSlotProxy proxy;
    proxy._body = data;

    data->name = std::move(name);
    data->pointsPendingUploaded.reserve(256);

    std::mt19937_64 mt{std::random_device{}()};
    std::uniform_real_distribution<float> range{0.3, 1};
    data->plotColor.w = 1.;
    data->plotColor.x = range(mt);
    data->plotColor.y = range(mt);
    data->plotColor.z = range(mt);

    _slots.push_back(std::move(data));
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
    if (CPPH_CLEANUP(&End); Begin("Time Plot List", &_widget.bShowListPanel, ImGuiWindowFlags_MenuBar))
    {
        /// Render window management menu
        if (CondInvoke(ImGui::BeginMenuBar(), &ImGui::EndMenuBar))
        {
            if (CondInvoke(ImGui::BeginMenu("Windows"), &ImGui::EndMenu))
            {
                if (ImGui::MenuItem("+ Create New"))
                {
                    _createNewPlotWindow();
                }
                else if (not _windows.empty())
                {
                    ImGui::Separator();
                    for (auto iter = _windows.begin(); iter != _windows.end();)
                    {
                        auto& wnd = *iter;
                        if (CondInvoke(ImGui::BeginMenu(usfmt("{}###{}", wnd->title, wnd->key)), &ImGui::EndMenu))
                        {
                            ImGui::Checkbox("Visiblity", &wnd->bIsDisplayed);
                            ImGui::SameLine();
                            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 80 * DpiScale());
                            bool bEraseWnd = ImGui::Button("delete", {-1, 0});
                            ImGui::InputText("Title", wnd->title);

                            if (bEraseWnd)
                            {
                                iter = _windows.erase(iter);
                                continue;
                            }
                        }

                        ++iter;
                    }
                }
            }
        }

        /// Render list of slots
        auto timeNow = steady_clock::now();

        for (auto& slot : _slots)
        {
            auto spinChars = "*|/-\\|/-"sv;
            bool bLatest = (timeNow - slot->timeLastUpload) < 1s;
            bool bFocusRenderedWindow = slot->bFocusRequested;
            slot->bFocusRequested = false;

            ImGui::PushID(slot.get());
            CPPH_CLEANUP(&ImGui::PopID);

            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable("##POPUP_SEL", false, ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_SpanAllColumns))
            {
                ImGui::OpenPopup("##POPUP_SEL");
            }
            else if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                bFocusRenderedWindow = true;
            }

            if (ImGui::SameLine(), ImGui::ColorButton("Graph Color", slot->plotColor))
            {
                ImGui::OpenPopup("##POPUP_COLOR");
            }

            ImGui::PushStyleColor(ImGuiCol_Text, bLatest ? ColorRefs::FrontOkay : ColorRefs::BackOkay);
            ImGui::SameLine();
            ImGui::Text("[%c]", spinChars[slot->uploadSequence % spinChars.size()]);
            ImGui::SameLine();

            if (auto wnd = slot->targetWindow.lock())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, bLatest ? ColorRefs::FrontWarn : ColorRefs::BackWarn);
                ImGui::SameLine();
                ImGui::Text("%s |", wnd->title.c_str());
                ImGui::PopStyleColor();

                if (bFocusRenderedWindow) { wnd->bRequestFocus = true, wnd->bIsDisplayed = true; }
            }

            ImGui::PushStyleColor(ImGuiCol_Text, bLatest ? ColorRefs::Enabled : ColorRefs::Disabled);
            ImGui::SameLine();
            ImGui::TextUnformatted(slot->name.c_str());

            ImGui::PopStyleColor(2);

            if (CondInvoke(ImGui::BeginPopup("##POPUP_SEL"), &EndPopup))
            {
                if (not slot->bDisableUserRemove && ImGui::MenuItem("Remove"))
                {
                    slot->bFocusRequested = true;
                }

                if (CondInvoke(ImGui::BeginMenu("View On"), &ImGui::EndMenu))
                {
                    if (ImGui::MenuItem("+ Create New"))
                    {
                        // Create new menu, and set this slot to be drawn on given window.
                        auto newWnd = _createNewPlotWindow();
                        slot->targetWindow = newWnd;
                        slot->bTargetWndChanged = true;
                        newWnd->bIsDisplayed = true;
                    }
                    else if (not _windows.empty())
                    {
                        ImGui::Separator();

                        for (auto& wnd : _windows)
                        {
                            if (ImGui::MenuItem(usprintf("%s##%p", wnd->title.c_str(), wnd.get())))
                            {
                                slot->targetWindow = wnd;
                                slot->bTargetWndChanged = true;
                                wnd->bIsDisplayed = true;
                            }
                        }
                    }
                }

                if (ImGui::MenuItem("Remove"))
                {
                    slot->bMarkDestroied = true;
                }
            }

            if (CondInvoke(ImGui::BeginPopup("##POPUP_COLOR"), &ImGui::EndPopup))
            {
                ImGui::SetColorEditOptions(ImGuiColorEditFlags_PickerHueWheel);

                ImGui::ColorPicker4("Plot Color", (float*)&slot->plotColor);
            }
        }  // for (auto& slot : _slots)
    }

    /// Make plot list
    for (auto& slot : _slots)
    {
        if (slot->bMarkDestroied) { continue; }

        if (auto wnd = slot->targetWindow.lock())
        {
            if (not wnd->bIsDisplayed) { continue; }
            wnd->plotsThisFrame.push_back(slot.get());
        }
    }

    /// Iterate each window, and display if needed.
    for (auto& wnd : _windows)
    {
        CPPH_FINALLY(wnd->plotsThisFrame.clear());

        if (not wnd->bIsDisplayed)
        {
            continue;
        }

        if (wnd->bRequestFocus)
        {
            wnd->bRequestFocus = false;
            // TODO: Focus here
        }

        bool bKeepOpen = true;
        ImGui::SetNextWindowSize({640, 480}, ImGuiCond_Once);
        if (CPPH_FINALLY(ImGui::End()); ImGui::Begin(usprintf("%s###%s", wnd->title.c_str(), wnd->key.c_str()), &bKeepOpen))
        {
            if (not bKeepOpen)
            {
                wnd->bIsDisplayed = false;
            }

            if (CondInvoke(ImPlot::BeginPlot(usprintf("%s##%p", wnd->title.c_str(), wnd.get()), {-1, -1}), ImPlot::EndPlot))
            {
                for (auto slot : wnd->plotsThisFrame)
                {
                    DrawPlotContent(slot);
                }
            }
        }
    }
}

void TimePlotWindowManager::DrawPlotContent(TimePlot::SlotData* slot)
{
}

void TimePlotWindowManager::_fnAsyncValidateCache()
{
    // Perform caching
    // TODO: To make auto-fit available, first and last point of data must be contained!

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

            bCacheInvalid |= (bool)refWindow->bDirty;
            bCacheInvalid |= (bool)slot->bTargetWndChanged;

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

auto TimePlotWindowManager::_createNewPlotWindow(string key) -> shared_ptr<TimePlot::WindowContext>
{
    if (key.empty())
    {
        key.resize(8);
        generate_random_characters(key.begin(), 8, std::random_device{});
    }

    auto ptr = make_shared<TimePlot::WindowContext>();
    ptr->title = fmt::format("Plot {}", ++_wndCreateIndexer);
    ptr->key = std::move(key);

    _windows.push_back(ptr);
    return ptr;
}
