//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include "interfaces/Session.hpp"

namespace perfkit::msgpack::rpc {
class context;
class if_context_monitor;
}  // namespace perfkit::msgpack::rpc

class BasicPerfkitNetClient : public ISession
{
    unique_ptr<perfkit::msgpack::rpc::context> _rpc;
    shared_ptr<perfkit::msgpack::rpc::if_context_monitor> _monitor;

   public:
    BasicPerfkitNetClient();
    ~BasicPerfkitNetClient();

   protected:
    //! @note Connection to server must be unique!
    perfkit::msgpack::rpc::context* GetRpc() { return &*_rpc; }

   public:

};
