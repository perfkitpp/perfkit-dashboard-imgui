#pragma once
#include "if_session_connection.hpp"
#include "perfkit/common/thread/locked.hxx"

class session_context
{
   public:
    session_context(connection_ptr conn);
    void login(std::string_view id, std::string_view pw);

    bool expired() const;

   private:
    void _on_recv(std::string_view route, nlohmann::json const& msg);

   private:
    connection_ptr _conn;
    perfkit::locked<std::string> _output;
};
