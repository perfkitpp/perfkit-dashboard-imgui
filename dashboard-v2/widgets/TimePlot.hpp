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
    // Is being destroied?
    // Will be disposed on next iteration.
    bool bMarkDestroied : 1;

    // Name of this node
    string name;

    // Index range in _cacheRender
    size_t cacheAxisRangeX[2] = {};

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
    } async;

   public:
    SlotData() noexcept : bMarkDestroied(false) {}
};
}  // namespace TimePlot

/**
 * Submits number and timestamp periodically
 *
 */
class TimePlotWindowManager
{
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
    void TickWindow();
    auto CreateSlot(string name) -> TimePlotSlotProxy;

   private:
    void _fnAsyncValidateCache();
    void _fnMainThreadSwapBuffer();
};