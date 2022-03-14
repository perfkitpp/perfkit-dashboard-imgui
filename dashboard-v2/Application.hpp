//
// Created by ki608 on 2022-03-14.
//

#pragma once

#include <memory>
#include <vector>

#include <perfkit/common/functional.hxx>

#include "interfaces/Session.hpp"

namespace asio {
class io_context;
}

class Application
{
    struct SessionNode
    {
        string Key;
        shared_ptr<ISession> Ref;

        bool bShow = false;
    };

   private:
    unique_ptr<asio::io_context> _ioc;
    vector<SessionNode> _sessions;

    // UI Flags - View
    bool _bDrawSessionList = true;

    // UI Flags - Debug
    bool _bShowMetrics = false;
    bool _bShowDemo    = false;

   public:
    static Application* Get();
    Application();
    ~Application();

   public:
    void TickMainThread();

   private:
    void drawMenuContents();
    void drawSessionList(bool* bKeepOpen) {}
    void tickSessions() {}

   public:
    //! Allows posting events from different thread
    //! \param callable
    void PostEvent(perfkit::function<void()> callable);
};
