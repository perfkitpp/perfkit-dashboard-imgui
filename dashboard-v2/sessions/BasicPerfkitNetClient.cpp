//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <perfkit/common/refl/extension/msgpack-rpc.hxx>

using namespace perfkit;

class PerfkitNetClientRpcMonitor : public msgpack::rpc::if_context_monitor
{
   public:
    BasicPerfkitNetClient* _owner = nullptr;

    void on_new_session(const msgpack::rpc::session_profile& profile) noexcept override
    {
        _owner->_onSessionCreate_(profile);
    }
    void on_dispose_session(const msgpack::rpc::session_profile& profile) noexcept override
    {
        _owner->_onSessionDispose_(profile);
    }
};

BasicPerfkitNetClient::BasicPerfkitNetClient()
{
    using namespace net::message;

    // Create service
    auto service = msgpack::rpc::service_info{};
    service.serve(
            notify::tty,
            [](tty_output_t& h) {
                printf("%s", h.content.c_str());
            });

    // Create monitor
    auto monitor    = std::make_shared<PerfkitNetClientRpcMonitor>();
    monitor->_owner = this;
    _monitor        = monitor;

    // Create RPC context
    _rpc = std::make_unique<msgpack::rpc::context>(
            std::move(service),
            [](auto&& fn) { asio::dispatch(std::forward<decltype(fn)>(fn)); },
            _monitor);
}

void BasicPerfkitNetClient::FetchSessionDisplayName(std::string* outName)
{
    if (not IsSessionOpen())
        return;

    outName->assign("Example");
}

void BasicPerfkitNetClient::RenderTickSession()
{
    // State summary (bandwidth, memory usage, etc ...)

    // Basic buttons for opening config/trace

    // List of available GUI window

    // Terminal
}

void BasicPerfkitNetClient::TickSession()
{
    ISession::TickSession();
}

BasicPerfkitNetClient::~BasicPerfkitNetClient() = default;
