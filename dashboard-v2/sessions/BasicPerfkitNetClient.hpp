//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include <TextEditor.h>
#include <perfkit/common/refl/msgpack-rpc/request_handle.hxx>
#include <perfkit/common/thread/locked.hxx>
#include <perfkit/common/timer.hxx>
#include <perfkit/extension/net/protocol.hpp>

#include "interfaces/RpcSessionOwner.hpp"
#include "interfaces/Session.hpp"
#include "widgets/ConfigWindow.hpp"

namespace perfkit::msgpack::rpc {
class context;
class if_context_monitor;
struct session_profile;
}  // namespace perfkit::msgpack::rpc

namespace message = perfkit::net::message;

class BasicPerfkitNetClient : public std::enable_shared_from_this<BasicPerfkitNetClient>,
                              public ISession,
                              public IRpcSessionOwner
{
    using RpcRequestHandle = perfkit::msgpack::rpc::request_handle;

    using service = perfkit::net::message::service;
    using notify = perfkit::net::message::notify;

   private:
    string _key;

    //
    unique_ptr<perfkit::msgpack::rpc::context>            _rpc;
    shared_ptr<perfkit::msgpack::rpc::if_context_monitor> _monitor;
    shared_ptr<void>                                      _rpcFlushGuard = std::make_shared<nullptr_t>();

    // Lifetime anchor of single session.
    shared_ptr<void> _sessionAnchor;

    //
    RpcRequestHandle _hrpcHeartbeat;

    // Login
    RpcRequestHandle      _hrpcLogin;
    message::auth_level_t _authLevel = message::auth_level_t::unauthorized;

    //
    perfkit::poll_timer _timHeartbeat{1s};

    //
    service::session_info_t  _sessionInfo;
    notify::session_status_t _sessionStats{};

    // TTY
    TextEditor              _tty;
    perfkit::locked<string> _ttyQueue;

    // Widgets
    widgets::ConfigWindow _wndConfig{this};

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

    auto RpcContext() -> perfkit::msgpack::rpc::context* override { return &*_rpc; }
    auto SessionAnchor() -> weak_ptr<void> override { return _sessionAnchor; }

   private:
    virtual void RenderSessionOpenPrompt() = 0;

   private:
    void tickHeartbeat();

    void drawTTY();
    void drawSessionStateBox();

   protected:
    //! @note Connection to server must be unique!
    perfkit::msgpack::rpc::context* GetRpc() { return &*_rpc; }

   public:
    void _onSessionCreate_(perfkit::msgpack::rpc::session_profile const&);
    void _onSessionDispose_(perfkit::msgpack::rpc::session_profile const&);
};
