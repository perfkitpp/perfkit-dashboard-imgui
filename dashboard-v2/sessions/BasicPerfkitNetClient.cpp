//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <perfkit/common/refl/msgpack-rpc/context.hxx>

using namespace perfkit;
using namespace net::message;

class PerfkitNetClientRpcMonitor : public msgpack::rpc::if_context_monitor
{
   public:
    std::weak_ptr<BasicPerfkitNetClient> _owner;

   public:
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
    // Create service
    auto service = msgpack::rpc::service_info{};
    service.serve(notify::tty,
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

    outName->clear();
    fmt::format_to(
            std::back_inserter(*outName), "{}@{}",
            _sessionInfo.name, _sessionInfo.hostname);
}

void BasicPerfkitNetClient::RenderTickSession()
{
    char tbuf[128];
    auto genId =
            [&](char const* tag) {

            };

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

    auto info   = decltype(service::session_info)::return_type{};
    auto result = service::session_info(*_rpc).rpc(&info, 1s);

    if (result != msgpack::rpc::rpc_status::okay)
    {
        NotifyToast{"Rpc invocation failed"}.Error().String(to_string(result));
        return;
    }

    PostEventMainThreadWeak(weak_from_this(),
                            [this, info = std::move(info)]() mutable {
                                _sessionInfo = std::move(info);
                            });
}

void BasicPerfkitNetClient::_onSessionDispose_(const msgpack::rpc::session_profile& profile)
{
    NotifyToast("Rpc Session Disposed").Wanrning().String(profile.peer_name);
    CloseSession();
}

BasicPerfkitNetClient::~BasicPerfkitNetClient()
{
    _rpc.reset();
}

void BasicPerfkitNetClient::tickHeartbeat()
{
    if (not _timHeartbeat.check()) { return; }

    if (_hrpcHeartbeat && not _hrpcHeartbeat.wait(0ms))
    {
        NotifyToast{"Heartbeat failed"};

        CloseSession();
        return;
    }

    auto onHeartbeat =
            [this](auto&& exception) {
                if (exception)
                    NotifyToast{"Heartbeat returned error"}
                            .Error()
                            .String(exception->what());
                else
                    NotifyToast{}.Trivial().String("Heartbeat!");
            };

    _hrpcHeartbeat = service::heartbeat(*_rpc).async_rpc(
            bind_front_weak(weak_from_this(), std::move(onHeartbeat)));
}

void BasicPerfkitNetClient::CloseSession()
{
    ISession::CloseSession();
    _hrpcHeartbeat.abort();
}
