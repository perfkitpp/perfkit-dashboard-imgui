//
// Created by ki608 on 2022-03-20.
//

#include "TimePlot.hpp"

#include "Application.hpp"
#include "cpph/helper/macros.hxx"
#include "cpph/utility/chrono.hxx"
#include "cpph/utility/cleanup.hxx"
#include "cpph/utility/random.hxx"
#include "imgui.h"
#include "imgui_extension.h"
#include "implot.h"
#include "perfkit/configs.h"

PERFKIT_DECLARE_SUBCATEGORY(GConfig::Widgets)
{
    struct PlotWindow {
        string key;
        string title;
        bool bIsDisplayed;
        bool bTimePlotMode;

        CPPH_REFL_DEFINE_OBJECT_inline_simple(key, title, bIsDisplayed, bTimePlotMode);
    };

    PERFKIT_CONFIGURE(TimePlotWindows, vector<PlotWindow>{}).confirm();
}

TimePlotWindowManager::TimePlotWindowManager()
{
    Application::Get()->OnLoadWorkspace +=
            [this] {
                auto& list = GConfig::Widgets::TimePlotWindows.ref();

                for (auto& l : list) {
                    auto newWnd = _createNewPlotWindow(l.key);
                    newWnd->title = l.title;
                    newWnd->bIsDisplayed = l.bIsDisplayed;
                    newWnd->bFollowGraphMovement = not l.bTimePlotMode;
                    newWnd->frameInfo.bTimeBuildMode = l.bTimePlotMode;
                }

                _widget.bShowListPanel = RefPersistentNumber("TimePlotPersistant");
            };

    Application::Get()->OnDumpWorkspace +=
            [this] {
                vector<GConfig::Widgets::PlotWindow> wnds;
                wnds.reserve(_windows.size());

                for (auto& wnd : _windows) {
                    auto& elem = wnds.emplace_back();
                    elem.key = wnd->key;
                    elem.title = wnd->title;
                    elem.bIsDisplayed = wnd->bIsDisplayed;
                    elem.bTimePlotMode = wnd->frameInfo.bTimeBuildMode;
                }

                GConfig::Widgets::TimePlotWindows.commit(wnds);
                RefPersistentNumber("TimePlotPersistant") = _widget.bShowListPanel;
            };

    //
    _cacheRender[0].reserve(1 << 16);
    _cacheRender[1].reserve(1 << 16);
    _async.cacheBuild[0].reserve(1 << 16);
    _async.cacheBuild[1].reserve(1 << 16);
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
    bool const bAsyncJobTriggerFrame = not _caching && _timerCacheTrig.check();
    bool const bCacheReceivedThisFrame = exchange(_cacheRecvFrame, false);
    if (bAsyncJobTriggerFrame) {
        _fnTriggerAsyncJob();
    }

    /// Render MenuBar
    if (CondInvoke(BeginMainMenuBar(), &EndMainMenuBar)) {
        if (CondInvoke(BeginMenu(LOCWORD("View")), &EndMenu)) {
            Separator();
            MenuItem("Time Plots", NULL, &_widget.bShowListPanel);
        }
    }

    /// Render Main Window

    if (not _widget.bShowListPanel)
        return;

    ImGui::SetNextWindowSize({640, 480}, ImGuiCond_Once);
    if (CPPH_CLEANUP(&End); Begin(LOCWORD("Time Plot List"), &_widget.bShowListPanel, ImGuiWindowFlags_MenuBar)) {
        /// Render window management menu
        if (CondInvoke(ImGui::BeginMenuBar(), &ImGui::EndMenuBar)) {
            if (CondInvoke(ImGui::BeginMenu(LOCWORD("Windows")), &ImGui::EndMenu)) {
                if (ImGui::MenuItem(LOCWORD("+ Create New"))) {
                    _createNewPlotWindow();
                } else if (not _windows.empty()) {
                    ImGui::Separator();
                    for (auto iter = _windows.begin(); iter != _windows.end();) {
                        auto& wnd = *iter;
                        if (CondInvoke(ImGui::BeginMenu(usfmt("{}###{}", wnd->title, wnd->key)), &ImGui::EndMenu)) {
                            ImGui::Checkbox(KEYTEXT(PLOT_VISIBLE_CHECKBOX, "Visiblity"), &wnd->bIsDisplayed);
                            ImGui::SameLine();
                            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 80 * DpiScale());
                            bool bEraseWnd = ImGui::Button(LOCWORD("delete"), {-1, 0});
                            ImGui::InputText(LOCWORD("Title"), wnd->title);

                            if (bEraseWnd) {
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

        for (auto& slot : _slots) {
            auto spinChars = "*|/-\\|/-"sv;
            bool bLatest = (timeNow - slot->timeLastUpload) < 1s;
            bool bFocusRenderedWindow = slot->bFocusRequested;
            slot->bFocusRequested = false;

            ImGui::PushID(slot.get());
            CPPH_CLEANUP(&ImGui::PopID);

            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable("##POPUP_SEL", false, ImGuiSelectableFlags_AllowItemOverlap | ImGuiSelectableFlags_SpanAllColumns)) {
                ImGui::OpenPopup("##POPUP_SEL");
            } else if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                bFocusRenderedWindow = true;
            }

            if (ImGui::BeginDragDropSource()) {
                size_t currentIndex = &slot - _slots.data();
                ImGui::SetDragDropPayload("DND_PLOT_SLOT_INDEX", &currentIndex, sizeof currentIndex);
                ImGui::TextUnformatted(slot->name.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::SameLine(), ImGui::ColorButton("Graph Color", slot->plotColor)) {
                ImGui::OpenPopup("##POPUP_COLOR");
            }

            ImGui::PushStyleColor(ImGuiCol_Text, bLatest ? ColorRefs::FrontOkay : ColorRefs::BackOkay);
            ImGui::SameLine();
            ImGui::Text("[%c]", spinChars[slot->uploadSequence % spinChars.size()]);
            ImGui::SameLine();

            if (auto wnd = slot->targetWindow.lock()) {
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

            if (CondInvoke(ImGui::BeginPopup("##POPUP_SEL"), &EndPopup)) {
                if (not slot->bDisableUserRemove && ImGui::MenuItem("Remove")) {
                    slot->bFocusRequested = true;
                }

                if (CondInvoke(ImGui::BeginMenu(LOCWORD("View On")), &ImGui::EndMenu)) {
                    if (ImGui::MenuItem(LOCWORD("+ Create New"))) {
                        // Create new menu, and set this slot to be drawn on given window.
                        auto newWnd = _createNewPlotWindow();
                        slot->targetWindow = newWnd;
                        slot->bTargetWndChanged = true;
                        newWnd->bIsDisplayed = true;
                    } else if (ImGui::MenuItem(LOCWORD("Hide"))) {
                        slot->targetWindow = {};
                    } else if (not _windows.empty()) {
                        ImGui::Separator();

                        for (auto& wnd : _windows) {
                            if (ImGui::MenuItem(usprintf("%s##%p", wnd->title.c_str(), wnd.get()))) {
                                slot->targetWindow = wnd;
                                slot->bTargetWndChanged = true;
                                wnd->bIsDisplayed = true;
                            }
                        }
                    }
                }

                if (ImGui::MenuItem(LOCWORD("Remove"))) {
                    slot->bMarkDestroied = true;
                }
            }

            if (CondInvoke(ImGui::BeginPopup("##POPUP_COLOR"), &ImGui::EndPopup)) {
                ImGui::SetColorEditOptions(ImGuiColorEditFlags_PickerHueWheel);

                ImGui::ColorPicker4(LOCWORD("Plot Color"), (float*)&slot->plotColor);
            }
        }  // for (auto& slot : _slots)
    }

    /// Make plot list
    for (auto& slot : _slots) {
        if (slot->bMarkDestroied) { continue; }

        if (auto wnd = slot->targetWindow.lock()) {
            if (not wnd->bIsDisplayed) { continue; }
            wnd->plotsThisFrame.push_back(slot.get());
        }
    }

    decltype(1.s) deltaTime;
    if (bCacheReceivedThisFrame) {
        deltaTime = _tmTimeplotDelta.elapsed();
        _tmTimeplotDelta.reset();
    }

    /// Iterate each window, and display if needed.
    for (auto& wnd : _windows) {
        CPPH_FINALLY(wnd->plotsThisFrame.clear());

        if (not wnd->bIsDisplayed) {
            continue;
        }

        if (wnd->bRequestFocus) {
            wnd->bRequestFocus = false;
            // TODO: Focus here
        }

        bool bKeepOpen = true;
        ImGui::SetNextWindowSize({640, 480}, ImGuiCond_Once);
        if (CPPH_FINALLY(ImGui::End()); ImGui::Begin(usprintf("%s###%s", wnd->title.c_str(), wnd->key.c_str()), &bKeepOpen, ImGuiWindowFlags_MenuBar)) {
            if (not bKeepOpen) {
                wnd->bIsDisplayed = false;
            }

            bool& bIsTimeBuildMode = wnd->frameInfo.bTimeBuildMode;

            if (CondInvoke(ImGui::BeginMenuBar(), &ImGui::EndMenuBar)) {
                ImGui::AlignTextToFramePadding();

                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
                ImGui::InputText("##Title", wnd->title);
                ImGui::Checkbox(LOCWORD("Time Plot"), &bIsTimeBuildMode);
                ImGui::Checkbox(usprintf("%s###CHKBOX", wnd->bFollowGraphMovement ? LOCWORD("Follow Plot") : LOCWORD("Fix Plot")), &wnd->bFollowGraphMovement);
            }

            if (CondInvoke(ImPlot::BeginPlot(usprintf("###%p", wnd->title.c_str(), wnd.get()), {-1, -1}), ImPlot::EndPlot)) {
                if (bCacheReceivedThisFrame && (bIsTimeBuildMode == wnd->bFollowGraphMovement)) {
                    auto [x, y] = wnd->frameInfo.rangeX;
                    auto dt = deltaTime.count();
                    if (not wnd->frameInfo.bTimeBuildMode) { dt = -dt; }
                    ImPlot::SetupAxisLimits(ImAxis_X1, x + dt, y + dt, ImPlotCond_Always);
                }

                // TODO: Make legend drag-droppable
                if (wnd->frameInfo.bTimeBuildMode) {
                    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);
                } else {
                    ImPlot::SetupAxisScale(ImAxis_X1, 0);
                }

                // Invalidate cache if window layout has changed.
                {
                    auto limits = ImPlot::GetPlotLimits();
                    auto finfo = &wnd->frameInfo;

                    auto& [x1, x2] = finfo->rangeX;
                    auto& [y1, y2] = finfo->rangeY;

                    y1 = limits.Y.Min;
                    y2 = limits.Y.Max;

                    if (abs(x1 - limits.X.Min) > 1e-6) {
                        wnd->bDirty = true;
                        x1 = limits.X.Min;
                    }
                    if (abs(x2 - limits.X.Max) > 1e-6) {
                        wnd->bDirty = true;
                        x2 = limits.X.Max;
                    }

                    if (double w = ImPlot::GetPlotSize().x; abs(finfo->displayPixelWidth - w) > 1e-6) {
                        wnd->bDirty = true;
                        finfo->displayPixelWidth = w;
                    }
                }

                for (auto slot : wnd->plotsThisFrame) {
                    DrawPlotContent(slot);
                }
            }

            if (ImGui::BeginDragDropTarget()) {
                if (auto payload = ImGui::AcceptDragDropPayload("DND_PLOT_SLOT_INDEX")) {
                    auto idx = *(size_t*)payload->Data;

                    if (idx < _slots.size()) {
                        _slots[idx]->targetWindow = wnd;
                        _slots[idx]->bTargetWndChanged = true;
                    }

                    ImGui::EndDragDropTarget();
                }
            }
        }
    }
}

void TimePlotWindowManager::DrawPlotContent(TimePlot::SlotData* slot)
{
    auto [i1, i2] = slot->cacheAxisRange;
    auto& [rX, rY] = _cacheRender;

    if (i2 - i1 == 0) { return; }

    ImPlot::PushStyleColor(ImPlotCol_Line, slot->plotColor);
    ImPlot::PlotLine(slot->name.c_str(), &rX[i1], &rY[i1], i2 - i1);

    ImPlot::PopStyleColor();
}

void TimePlotWindowManager::_fnAsyncValidateCache()
{
    // Perform caching
    // To make auto-fit available, first and last point of data must be contained!
    auto bx = _async.cacheBuild + 0;
    auto by = _async.cacheBuild + 1;

    bx->clear(), by->clear();
    auto now = steady_clock::now();
    auto sysNow = system_clock::now();
    auto nowD = now.time_since_epoch();
    auto sysNowD = sysNow.time_since_epoch();
    auto tzone = duration_cast<steady_clock::duration>(timezone_offset());

    // Iterate slots, cache plot window frame ranges
    for (auto& slot : _async.targets) {
        auto slotCtx = &slot->async;
        auto const& finfo = slotCtx->frameInfo;
        auto const bTimeBuild = finfo.bTimeBuildMode;
        auto& [i1, i2] = slotCtx->cacheAxisRange;
        i1 = i2 = bx->size();

        // sample / pixel
        auto numSampleLeftL = size_t(finfo.displayPixelWidth);
        auto allV = &slotCtx->allValues;
        auto sbeg = allV->begin(), send = allV->end();

        // Sample within given window range
        auto [dmin, dmax] = finfo.rangeX;
        steady_clock::time_point xmin, xmax;

        if (bTimeBuild) {
            xmin = steady_clock::time_point{} + duration_cast<system_clock::duration>(1.s * dmin) - sysNowD + nowD - tzone;
            xmax = steady_clock::time_point{} + duration_cast<system_clock::duration>(1.s * dmax) - sysNowD + nowD - tzone;
        } else {
            xmin = now + duration_cast<steady_clock::duration>(1.s * dmin);
            xmax = now + duration_cast<steady_clock::duration>(1.s * dmax);
        }

        // Give some margin both side
        auto margin = (xmax - xmin) / 20;
        xmin -= margin, xmax += margin;

        if (numSampleLeftL == 0) { continue; }
        if (sbeg == send) { continue; }

        // Search lower bound from remaining contents
        for (; sbeg != send && numSampleLeftL; --numSampleLeftL) {
            // Store cached value
            by->push_back(sbeg->value);

            if (bTimeBuild)
                bx->push_back(to_seconds(sbeg->timestamp.time_since_epoch() - nowD + sysNowD + tzone));
            else
                bx->push_back(to_seconds(sbeg->timestamp - now));

            // Determine next time-point by uniform division
            xmin = xmin + (xmax - xmin) / numSampleLeftL;

            // Binary search next node.
            sbeg = std::lower_bound(sbeg + 1, send, xmin);
        }

        // Must push last element, to make autofit available.
        // First element will automatically be added by above loop logic.
        by->push_back(allV->back().value);

        if (bTimeBuild)
            bx->push_back(to_seconds(allV->back().timestamp.time_since_epoch() - nowD + sysNowD + tzone));
        else
            bx->push_back(to_seconds(allV->back().timestamp - now));

        // Correct cache axis range
        i2 = bx->size();
    }

    // Request swap buffer on main thread.
    PostEventMainThread(bind(&TimePlotWindowManager::_fnMainThreadSwapBuffer, this));
}

void TimePlotWindowManager::_fnMainThreadSwapBuffer()
{
    VerifyMainThread();
    _caching = false;
    _cacheRecvFrame = true;

    // Swap build/render buffer here.
    swap(_cacheRender[0], _async.cacheBuild[0]);
    swap(_cacheRender[1], _async.cacheBuild[1]);

    // Swap slots ranges ...
    for (auto& slot : _slots) {
        slot->cacheAxisRange[0] = slot->async.cacheAxisRange[0];
        slot->cacheAxisRange[1] = slot->async.cacheAxisRange[1];
    }
}

void TimePlotWindowManager::_fnTriggerAsyncJob()
{
    VerifyMainThread();

    bool bHasAnyInvalidCache = false;
    _async.targets.clear();

    for (auto iter = _slots.begin(); iter != _slots.end();) {
        if ((**iter).bMarkDestroied) {
            iter = _slots.erase(iter);
        } else {
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
            auto& allV = async.allValues;
            auto& queued = slot->pointsPendingUploaded;

            size_t constexpr MAX_ENTITY = 2'000'000;
            if (allV.capacity() - allV.size() < queued.size() && allV.capacity() < MAX_ENTITY) {
                allV.reserve_shrink(min(MAX_ENTITY, allV.capacity() * 4));
            }

            async.frameInfo = refWindow->frameInfo;
            allV.enqueue_n(queued.begin(), queued.size());

            slot->pointsPendingUploaded.clear();
            _async.targets.push_back(slot);
        }
    }

    // Iterate windows, clear dirty flag
    for (auto& wnd : _windows) {
        wnd->bDirty = false;
    }

    // Trigger async job
    if (bHasAnyInvalidCache) {
        _caching = true;
        _asyncWorker.post(bind(&TimePlotWindowManager::_fnAsyncValidateCache, this));
    }
}

auto TimePlotWindowManager::_createNewPlotWindow(string key) -> shared_ptr<TimePlot::WindowContext>
{
    if (key.empty()) {
        key.resize(8);
        generate_random_characters(key.begin(), 8, std::random_device{});
    }

    auto ptr = make_shared<TimePlot::WindowContext>();
    ptr->title = fmt::format(LOCWORD("Plot {}"), ++_wndCreateIndexer);
    ptr->key = std::move(key);

    _windows.push_back(ptr);
    return ptr;
}
