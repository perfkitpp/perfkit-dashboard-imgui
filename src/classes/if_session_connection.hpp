// abstract session connection
#pragma once
#include <functional>

#include <nlohmann/json.hpp>

using connection_ptr = std::shared_ptr<class if_session_connection>;

class if_session_connection
{
   public:
    virtual ~if_session_connection() = default;

    std::function<void(std::string_view, nlohmann::json const&)>
            received_message_handler;

    virtual void send_message(
            std::string_view route, nlohmann::json const& parameter) {}

    virtual bool connection_valid() const { return true; }
};
