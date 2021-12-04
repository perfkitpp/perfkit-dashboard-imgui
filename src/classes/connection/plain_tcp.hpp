//
// Created by ki608 on 2021-11-28.
//

#pragma once
#include <asio/ip/tcp.hpp>
#include <perfkit/logging.h>

#include "../if_session_connection.hpp"

class plain_tcp : public if_session_connection
{
   public:
    plain_tcp(asio::io_context* ioc, char const* address, uint16_t port);
    ~plain_tcp() override;

    void send_message(
            std::string_view route, const nlohmann::json& parameter) override;

    session_connection_state status() const override;

   private:
    void _on_connect(asio::error_code const& ec);

    void _handle_header(asio::error_code const& ec, size_t num_read);
    void _handle_body(asio::error_code const& ec, size_t num_read);

   private:
    auto CPPH_LOGGER() const { return &*_logging; }

    auto _rd() { return asio::mutable_buffer(_rdbuf.data(), _rdbuf.size()); }
    auto _wr() const { return asio::const_buffer(_wrbuf.data(), _wrbuf.size()); }

   private:
    asio::ip::tcp::socket _socket;
    asio::ip::tcp::endpoint _endpoint;

    std::atomic_bool _is_valid = false;
    std::vector<char> _wrbuf;
    std::vector<char> _rdbuf;

    session_connection_state _status = session_connection_state::invalid;
    nlohmann::json _sendbuf;
    std::shared_ptr<void> _wr_lock;

    perfkit::logger_ptr _logging = perfkit::share_logger("tcp:plain");
};
