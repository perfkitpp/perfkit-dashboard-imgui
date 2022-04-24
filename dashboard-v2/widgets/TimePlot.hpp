//
// Created by ki608 on 2022-03-20.
//

#pragma once
#include "cpph/circular_queue.hxx"
#include "cpph/thread/thread_pool.hxx"
#include "cpph/timer.hxx"
#include "imgui.h"
#include "utils/TimePlotSlotProxy.hpp"

namespace TimePlot {
using std::chrono::steady_clock;
class WindowContext;

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
    double displayPixelWidth = 0;
};

/**
 * Contains plotting context
 */
struct SlotData
{
    // Is being destroied?
    // Will be disposed on next iteration.
    bool bMarkDestroied : 1;

    // Target window changed ... should be recached !
    bool bTargetWndChanged : 1;

    // Focus is requests
    bool bFocusRequested : 1;

    // Do not expose remove control
    bool bDisableUserRemove : 1;

    // Name of this node
    string name;

    // Index range in _cacheRender
    size_t cacheAxisRangeX[2] = {};

    // Upload sequence index.
    size_t uploadSequence = 0;

    // Latest upload
    steady_clock::time_point timeLastUpload;

    vector<Point> pointsPendingUploaded;  // Modify on main thread -> Loop Thread
    weak_ptr<WindowContext> targetWindow;

    // Plotting color
    ImVec4 plotColor = {};

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

struct WindowContext
{
    // Hash. Used to generate window.
    string key;

    // Window name
    string title;

    //
    WindowFrameDescriptor frameInfo = {};

    //
    vector<SlotData*> plotsThisFrame;

    // Displaying ?
    bool bIsDisplayed = false;

    // State has changed?
    bool bDirty : 1;

    // Request focus on next frame
    bool bRequestFocus : 1;
};

}  // namespace TimePlot

/**
 * Submits number and timestamp periodically
 *
 */
class TimePlotWindowManager
{
    // All slot instances
    vector<shared_ptr<TimePlot::SlotData>> _slots;

    // To not reinitialize the whole array every time ...
    vector<double> _cacheRender;

    // Async work thread
    thread_pool _asyncWorker{1};

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

    // Widget context
    struct WidgetContext
    {
        // Show main panel ?
        bool bShowListPanel = false;
    } _widget;

    // List of window contexts
    vector<shared_ptr<TimePlot::WindowContext>> _windows;
    size_t _wndCreateIndexer = 0;

   public:
    TimePlotWindowManager();
    void TickWindow();
    auto CreateSlot(string name) -> TimePlotSlotProxy;

    // Must be inside of
    void DrawPlotContent(TimePlot::SlotData*);

   private:
    void _fnTriggerAsyncJob();
    void _fnAsyncValidateCache();
    void _fnMainThreadSwapBuffer();

   private:
    auto _createNewPlotWindow(string uniqueId = {}) -> shared_ptr<TimePlot::WindowContext>;
};
