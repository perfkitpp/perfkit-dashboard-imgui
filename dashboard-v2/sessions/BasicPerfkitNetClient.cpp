//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <perfkit/common/refl/extension/msgpack-rpc.hxx>

using namespace perfkit;
using namespace net::message;

class PerfkitNetClientRpcMonitor : public msgpack::rpc::if_context_monitor
{
   public:
    std::weak_ptr<BasicPerfkitNetClient> _owner;

    void on_new_session(const msgpack::rpc::session_profile& profile) noexcept override
    {
        if (auto lc = _owner.lock())
            lc->_onSessionCreate_(profile);
    }
    void on_dispose_session(const msgpack::rpc::session_profile& profile) noexcept override
    {
        if (auto lc = _owner.lock())
            lc->_onSessionDispose_(profile);
    }
};

BasicPerfkitNetClient::BasicPerfkitNetClient()
{
    _hrpcHeartbeat = std::make_unique<msgpack::rpc::rpc_wait_handle>();

    // Create service
    auto service = msgpack::rpc::service_info{};
    service.serve(
            notify::tty,
            [](tty_output_t& h) {
                printf("%s", h.content.c_str());
                fflush(stdout);
            });

    // Create monitor
    auto monitor = std::make_shared<PerfkitNetClientRpcMonitor>();
    _monitor     = monitor;

    // Create RPC context
    _rpc = std::make_unique<msgpack::rpc::context>(
            std::move(service),
            [](auto&& fn) { asio::dispatch(std::forward<decltype(fn)>(fn)); },
            _monitor);
}

void BasicPerfkitNetClient::InitializeSession(const string& keyUri)
{
    ((PerfkitNetClientRpcMonitor*)&*_monitor)->_owner = weak_from_this();
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
    if (not IsSessionOpen()) { return; }

    tickHeartbeat();
}

void BasicPerfkitNetClient::_onSessionCreate_(const msgpack::rpc::session_profile& profile)
{
    NotifyToast("Rpc Session Created").String(profile.peer_name);
}

void BasicPerfkitNetClient::_onSessionDispose_(const msgpack::rpc::session_profile& profile)
{
    NotifyToast("Rpc Session Disposed").String(profile.peer_name);
    CloseSession();
}

BasicPerfkitNetClient::~BasicPerfkitNetClient()
{
    _rpc.reset();
}

void BasicPerfkitNetClient::tickHeartbeat()
{
    if (not _timHeartbeat.check_sparse()) { return; }
    auto& handle = *_hrpcHeartbeat;

    if (not handle)
    {
        handle = service::heartbeat(*_rpc).rpc_async(nullptr);

        if (not handle)
            CloseSession();
    }
    else
    {
        auto waitResult = _rpc->rpc_wait(&*_hrpcHeartbeat, 0ms);
        if (waitResult == perfkit::msgpack::rpc::rpc_status::timeout)
        {
            _rpc->rpc_abort(std::move(handle));
            CloseSession();
        }
        else
        {
            NotifyToast{}.String("Heartbeat! {}", waitResult);
        }
    }
}

void BasicPerfkitNetClient::CloseSession()
{
    ISession::CloseSession();
    *_hrpcHeartbeat = {};
}
