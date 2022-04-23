#include "BasicPerfkitNetClient.hpp"
#include "cpph/refl/rpc/connection/asio.hxx"
#include "cpph/refl/rpc/protocol/msgpack-rpc.hxx"
#include "cpph/refl/rpc/session_builder.hxx"
#include "cpph/thread/thread_pool.hxx"

using namespace cpph;

class EventProcedureImpl : public rpc::if_event_proc
{
    cpph::thread_pool _eq_rpc{1, 20 << 10};
    cpph::thread_pool _eq_hdl{1, 20 << 10};

   public:
    void post_rpc_completion(function<void()>&& fn) override
    {
        _eq_rpc.post(std::move(fn));
    }

    void post_handler_callback(function<void()>&& fn) override
    {
        _eq_hdl.post(std::move(fn));
    }

    void post_internal_message(function<void()>&& fn) override
    {
        asio::post(std::move(fn));
    }
};

void BasicPerfkitNetClient::NotifyNewConnection(unique_ptr<perfkit::rpc::if_connection> newConn)
{
    static auto _eventProc = make_shared<EventProcedureImpl>();
    rpc::session_ptr newSession;

    // Discard return pointer, as monitor callback to handle new session
    rpc::session::builder{}
            .connection(std::move(newConn))
            .service(_notify_handler)
            .event_procedure(_eventProc)
            .protocol(make_unique<rpc::protocol::msgpack>())
            .monitor(_monitor)
            .enable_request()
            .build_to(newSession);

    _onSessionCreate_(newSession->profile());
}
