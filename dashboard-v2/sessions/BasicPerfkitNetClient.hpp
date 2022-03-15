//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include <perfkit/extension/net/protocol.hpp>

#include "interfaces/Session.hpp"

namespace perfkit::msgpack::rpc {
class context;
class if_context_monitor;
struct session_profile;
}  // namespace perfkit::msgpack::rpc

class BasicPerfkitNetClient : public std::enable_shared_from_this<BasicPerfkitNetClient>, public ISession
{
    unique_ptr<perfkit::msgpack::rpc::context> _rpc;
    shared_ptr<perfkit::msgpack::rpc::if_context_monitor> _monitor;

   public:
    BasicPerfkitNetClient();
    ~BasicPerfkitNetClient();

    void FetchSessionDisplayName(std::string*) final;
    void RenderTickSession() final;
    void TickSession() final;

   protected:
    //! @note Connection to server must be unique!
    perfkit::msgpack::rpc::context* GetRpc() { return &*_rpc; }

   public:
    void _onSessionCreate_(perfkit::msgpack::rpc::session_profile const&) {}
    void _onSessionDispose_(perfkit::msgpack::rpc::session_profile const&) {}
};
