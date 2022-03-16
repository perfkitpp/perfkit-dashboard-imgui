//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include <perfkit/common/refl/msgpack-rpc/request_handle.hxx>
#include <perfkit/common/timer.hxx>
#include <perfkit/extension/net/protocol.hpp>

#include "interfaces/Session.hpp"

namespace perfkit::msgpack::rpc {
class context;
class if_context_monitor;
struct session_profile;
}  // namespace perfkit::msgpack::rpc

class BasicPerfkitNetClient : public std::enable_shared_from_this<BasicPerfkitNetClient>, public ISession
{
    unique_ptr<perfkit::msgpack::rpc::context>            _rpc;
    shared_ptr<perfkit::msgpack::rpc::if_context_monitor> _monitor;

    //
    perfkit::msgpack::rpc::request_handle _hrpcHeartbeat;

    //
    perfkit::poll_timer _timHeartbeat{1s};

    //
    using service = perfkit::net::message::service;
    service::session_info_t _session_info;

   public:
    BasicPerfkitNetClient();
    ~BasicPerfkitNetClient();

    void FetchSessionDisplayName(std::string*) final;
    void RenderTickSession() final;
    void TickSession() override;
    void CloseSession() override;
    void InitializeSession(const string& keyUri) override;

   private:
    void tickHeartbeat();

   protected:
    //! @note Connection to server must be unique!
    perfkit::msgpack::rpc::context* GetRpc() { return &*_rpc; }

   public:
    void _onSessionCreate_(perfkit::msgpack::rpc::session_profile const&);
    void _onSessionDispose_(perfkit::msgpack::rpc::session_profile const&);
};
