//
// Created by ki608 on 2022-03-14.
//

#pragma once

#include <memory>
#include <vector>

#include <cpph/event.hxx>
#include <cpph/utility/singleton.hxx>

namespace asio {
class io_context;
}

class TimePlotWindowManager;
enum class ESessionType : int;

class Application
{
    struct SessionNode
    {
        string Key;
        ESessionType Type;
        shared_ptr<class ISession> Ref;

        bool bShow = false;
        bool bPendingClose = false;
        bool bTransient = false;  // Save session to

        string CachedDisplayName;
    };

   public:
    perfkit::event<> OnLoadWorkspace;
    perfkit::event<> OnDumpWorkspace;

   private:
    unique_ptr<asio::io_context> _ioc;
    vector<SessionNode> _sessions;
    std::string _workspacePath;
    bool _workspacePathChanged = true;

    // UI Flags - View
    bool _bDrawSessionList = true;

    // UI Flags - Debug
    bool _bShowMetrics = false;
    bool _bShowDemo = false;
    bool _bShowStyles = false;

    // UI State - Add New Session
    struct
    {
        bool bActivateButton = false;
        bool bSetNextFocusToInput = false;
        ESessionType Selected = {};
        char UriBuffer[1024] = {};
    } _addSessionModalState;

    // Time plot window manager
    unique_ptr<TimePlotWindowManager> _timePlot;

   public:
    static Application* Get();

    Application();
    ~Application();

    void Initialize();

   public:
    //! Ticks application on main thread.
    void TickMainThread();

    //! Register session with given key ...
    //! All sessions must be
    SessionNode* RegisterSessionMainThread(
            string keyString,
            ESessionType type,
            string_view optionalDefaultDisplayName = {},
            bool bTransient = false);

    //! Save workspace
    void SaveWorkspaceMainThread() { saveWorkspace(); }

    //! Get Timeplot Window Manager
    auto TimePlotManager() { return _timePlot.get(); }

   private:
    void tickGraphicsMainThread();

    void drawMenuContents();
    void drawSessionList(bool* bKeepOpen);
    void tickSessions();

    void drawAddSessionMenu();
    void loadWorkspace();
    void saveWorkspace();

   private:
    bool isSessionExist(std::string_view name, ESessionType type);

   public:
    //! Allows posting events from different thread
    //! \param callable
    void PostMainThreadEvent(perfkit::function<void()> callable);

    //! Dispatch
    void DispatchMainThreadEvent(perfkit::function<void()> callable);
};

constexpr auto gApp = perfkit::singleton_t<Application>{};
