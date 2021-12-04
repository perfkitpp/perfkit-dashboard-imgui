#pragma once
#include "if_session_connection.hpp"
#include "messages.hpp"
#include "perfkit/common/assert.hxx"
#include "perfkit/common/functional.hxx"
#include "perfkit/common/hasher.hxx"
#include "perfkit/common/thread/locked.hxx"

struct session_context_message_marshal_error : std::exception
{
};

namespace messages = perfkit::terminal::net;

class session_context
{
   public:
    using info_type = perfkit::terminal::net::outgoing::session_reset;

   public:
    session_context(connection_ptr conn);
    void login(std::string_view id, std::string_view pw);

    session_connection_state status() const noexcept { return _conn->status(); }
    info_type const* info() const noexcept;

    auto& shell_output() const { return _output; }

   private:
    void _on_recv(std::string_view route, nlohmann::json const& msg);

    template <class MsgTy_, typename Handler_>
    void _install(Handler_&& h)
    {
        auto wrapped_handler{
                [h = std::forward<Handler_>(h), message = MsgTy_{}]  //
                (nlohmann::json const& object) mutable
                {
                    try
                    {
                        object.get_to(message);
                        h(message);
                    }
                    catch (std::exception&)
                    {
                        throw session_context_message_marshal_error{};
                    }
                }};

        auto is_new = _handlers.try_emplace(
                                       perfkit::hasher::fnv1a_64(
                                               MsgTy_::ROUTE,
                                               MsgTy_::ROUTE + strlen(MsgTy_::ROUTE)),
                                       std::move(wrapped_handler))
                              .second;

        assert_(is_new);
    }

    void _on_epoch(info_type& payload);
    void _on_session_state(messages::outgoing::session_state const& payload);
    void _on_shell_output(messages::outgoing::shell_output const& payload);

   private:
    connection_ptr _conn;
    perfkit::locked<std::string> _output;
    std::optional<info_type> _info;

    std::unordered_map<
            uint64_t,
            perfkit::function<void(nlohmann::json const&)>>
            _handlers;
};
