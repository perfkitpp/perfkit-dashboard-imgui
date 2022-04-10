#include "BasicPerfkitNetClient.hpp"
#include "perfkit/common/refl/rpc/connection/asio.hxx"
#include "perfkit/common/refl/rpc/protocol/msgpack-rpc.hxx"
#include "perfkit/common/refl/rpc/session_builder.hxx"

using namespace perfkit;

void BasicPerfkitNetClient::NotifyNewConnection(unique_ptr<perfkit::rpc::if_connection> newConn)
{
    rpc::session_ptr newSession;

    // Discard return pointer, as monitor callback to handle new session
    rpc::session::builder{}
            .connection(std::move(newConn))
            .service(_notify_handler)
            .event_procedure(rpc::asio_global_event_procedure())
            .protocol(make_unique<rpc::protocol::msgpack>())
            .monitor(_monitor)
            .enable_request()
            .build_to(newSession);

    _onSessionCreate_(newSession->profile());
}
