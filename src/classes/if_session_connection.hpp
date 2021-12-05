// abstract session connection
#pragma once
#include <functional>

#include <nlohmann/json.hpp>

using connection_ptr = std::shared_ptr<class if_session_connection>;

enum class session_connection_state
{
    invalid,
    connecting,
    connected
};

class if_session_connection
{
   public:
    virtual ~if_session_connection() = default;

    std::function<void(std::string_view, nlohmann::json const&)>
            received_message_handler;

    virtual void send_message(
            std::string_view route, nlohmann::json const& parameter) {}

    virtual session_connection_state status() const { return session_connection_state::invalid; }

    template <typename MsgTy_>
    void send(MsgTy_&& msg)
    {
        send_message(
                std::remove_reference_t<MsgTy_>::ROUTE,
                std::forward<MsgTy_>(msg));
    }
};
