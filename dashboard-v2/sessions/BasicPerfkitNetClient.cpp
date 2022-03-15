//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <perfkit/common/refl/extension/msgpack-rpc.hxx>
#include <perfkit/extension/net/protocol.hpp>

using namespace perfkit;

BasicPerfkitNetClient::BasicPerfkitNetClient()
{
    using namespace net::message;
    auto service = msgpack::rpc::service_info{};
    service.serve(
            notify::tty,
            [](perfkit::net::message::tty_output_t&) {

            });
}

BasicPerfkitNetClient::~BasicPerfkitNetClient() = default;
