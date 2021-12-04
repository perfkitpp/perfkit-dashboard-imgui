#pragma once
#include "if_session_connection.hpp"
#include "messages.hpp"
#include "perfkit/common/thread/locked.hxx"

class session_context
{
   public:
    using info_type = perfkit::terminal::net::outgoing::session_reset;

   public:
    session_context(connection_ptr conn);
    void login(std::string_view id, std::string_view pw);

    session_connection_state status() const noexcept { return _conn->status(); }
    info_type const* session_info() const noexcept;

   private:
    void _on_recv(std::string_view route, nlohmann::json const& msg);

   private:
    connection_ptr _conn;
    perfkit::locked<std::string> _output;
    std::optional<info_type> _info;
};
