//
// Created by ki608 on 2022-03-20.
//

#pragma once
#include "cpph/circular_queue.hxx"
#include "cpph/thread/thread_pool.hxx"
#include "cpph/timer.hxx"
#include "utils/TimePlotSlotProxy.hpp"

namespace TimePlot {
using std::chrono::steady_clock;

/**
 * Indicates single dot on plot
 */
struct Point
{
    steady_clock::time_point timestamp = {};
    double value = 0;
};

/**
 * A plotting window
 *
 * Once plotting window's frame moves,
 */
class WindowFrameDescriptor
{
    double rangeX[2] = {};
    double rangeY[2] = {};

    // window_width / range
    double displayDensityX = 0;

    bool bDirty = false;
};

/**
 * Contains plotting context
 */
struct SlotData
{
    string name;
    weak_ptr<void> onlineAnchor;

    size_t cacheAxisRangeX[2] = {};
    size_t cacheAxisRangeY[2] = {};

    vector<Point> pointsPendingUploaded;  // Modify on main thread -> Loop Thread

    weak_ptr<WindowFrameDescriptor> targetWindow;

    struct AsyncContext
    {
        // Uploaded from main thread
        circular_queue<Point> allValues{50'000};

        // Target window info
        WindowFrameDescriptor frameInfo;

        // Will be downloaded on junction
        size_t cacheAxisRangeX[2] = {};
        size_t cacheAxisRangeY[2] = {};
    } async;
};
}  // namespace TimePlot

/**
 * Submits number and timestamp periodically
 *
 */
class TimePlotWindowManager
{
    /*
     * 1. 메인 스레드에서 각 슬롯 프록시가 데이터 업로드 -> SlotData.pointsPendingUpload
     * 2. 각 프레임 윈도우 서술자 업데이트 (가시 영역). 변경 시 dirty 마크
     * 3. 주기적으로 메인 스레드에서 각 슬롯 iterate -> 타겟 윈도우가 dirty하거나, 업로드된 데이터 있으면
     *    async.targets에 해당 슬롯데이터 복사 및 async.allValues에 업로드 된 데이터 복사.
     * as.4. 각 슬롯의 allValues를 윈도우의 프레임 영역에 맞게 샘플링하고(밀도 고려),
     */

    // All slot instances
    set<shared_ptr<TimePlot::SlotData>, std::owner_less<>> _slots;

    // To not reinitialize the whole array every time ...
    vector<double> _cacheRender;

    // Async work thread
    thread_pool _worker{1};

    // Async cache context
    // Only accessible from async thread
    struct AsyncContext
    {
        // List of cache targets
        vector<shared_ptr<TimePlot::SlotData>> targets;

        // Cache that is being built.
        // Exchanged on thread junction, and may not access from main thread after.
        vector<double> cacheBuild;
    } _async;

    // Timer for cache revalidation, and a flag to prevent duplicated request.
    bool _caching = false;
    poll_timer _timerCacheTrig = {100ms};

   public:
    void TickWindow() {}

    auto CreateSlot(string name) -> TimePlotSlotProxy;

   private:
    void _fnAsyncValidateCache();
    void _fnMainThreadSwapBuffer();
};
