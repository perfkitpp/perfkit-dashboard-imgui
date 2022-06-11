//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include <TextEditor.h>
#include <cpph/container/circular_queue.hxx>
#include <cpph/refl/rpc/core.hxx>
#include <cpph/refl/rpc/detail/service.hxx>
#include <cpph/thread/locked.hxx>
#include <cpph/utility/timer.hxx>
#include <perfkit/extension/net/protocol.hpp>

#include "interfaces/RpcSessionOwner.hpp"
#include "interfaces/Session.hpp"
#include "widgets/ConfigWindow.hpp"
#include "widgets/TraceWindow.hpp"

using namespace perfkit;
namespace message = net::message;

class BasicPerfkitNetClient : public std::enable_shared_from_this<BasicPerfkitNetClient>,
                              public ISession,
                              public IRpcSessionOwner
{
    using service = net::message::service;
    using notify = net::message::notify;

   private:
    string _key;
    string _displayKey;

    //
    shared_ptr<rpc::session> _rpc;
    shared_ptr<rpc::if_session_monitor> _monitor;
    shared_ptr<void> _rpcFlushGuard = std::make_shared<nullptr_t>();

    rpc::service _notify_handler = rpc::service::empty_service();

    // Lifetime anchor of single session.
    shared_ptr<void> _sessionAnchor;

    //
    rpc::request_handle _hrpcHeartbeat;

    // Login
    rpc::request_handle _hrpcLogin;
    message::auth_level_t _authLevel = message::auth_level_t::unauthorized;

    //
    poll_timer _timHeartbeat{1s};
    int _heartbeatFailCount = 0;

    //
    service::session_info_t _sessionInfo;
    notify::session_status_t _sessionStats{};

    // TTY
    TextEditor _tty;
    locked<string> _ttyQueue;

    // Widgets
    widgets::ConfigWindow _wndConfig{this};
    widgets::TraceWindow _wndTrace{this};

    // Flags
    struct
    {
        bool bConfigOpen = false;
        bool bTraceOpen = false;
        bool bGraphicsOpen = false;
    } _uiState;

   public:
    BasicPerfkitNetClient();
    ~BasicPerfkitNetClient() override;

    void FetchSessionDisplayName(std::string*) final;
    void RenderTickSession() final;
    void TickSession() override;
    void CloseSession() override;
    void InitializeSession(const string& keyUri) override;
    bool ShouldRenderSessionListEntityContent() const final;
    void RenderSessionListEntityContent() final;

    auto RpcSession() -> rpc::session* override { return &*_rpc; }
    auto SessionAnchor() -> weak_ptr<void> override { return _sessionAnchor; }
    auto KeyString() const -> string const& override { return _key; }
    auto DisplayString() const -> string const& override { return _displayKey; }

   private:
    virtual void RenderSessionOpenPrompt() = 0;

   private:
    void tickHeartbeat();

    void drawTTY();
    void drawSessionStateBox();

   protected:
    //! @note Connection to server must be unique!
    auto GetRpc() { return _rpc.get(); }

    //! Call on new connection created.
    //! It's better to be invoked from other than main thread.
    void NotifyNewConnection(unique_ptr<rpc::if_connection> newConn);

   public:
    void _onSessionCreate_(rpc::session_profile_view);
    void _onSessionDispose_(rpc::session_profile_view);
};
