//
// Created by ki608 on 2021-11-28.
//

#include "plain_tcp.hpp"

#include <asio/post.hpp>
#include <asio/read.hpp>
#include <perfkit/common/macros.hxx>
#include <spdlog/spdlog.h>

void plain_tcp::send_message(std::string_view route, const nlohmann::json &parameter)
{
}

bool plain_tcp::connection_valid() const
{
    return _socket.is_open() && _is_valid;
}

plain_tcp::plain_tcp(asio::io_context *ioc, const char *address, uint16_t port)
        : _socket{*ioc}
{
    _socket.open(asio::ip::tcp::v4());

    auto ep = asio::ip::tcp::endpoint{asio::ip::make_address(address), port};
    _socket.async_connect(ep, CPPH_BIND(_on_connect));
}

void plain_tcp::_on_connect(const asio::error_code &ec)
{
    _rdbuf.resize(8);
    _is_valid = true;

    asio::async_read(
            _socket, _rd(),
            asio::transfer_all(),
            CPPH_BIND(_handle_header));
}

void plain_tcp::_handle_header(asio::error_code const &ec, size_t num_read)
{
    if (ec)
    {  // any error
        auto ep = _socket.remote_endpoint();
        CPPH_ERROR("{}:{} connection aborted: ({}) {}",
                   ep.address().to_string(),
                   ep.port(),
                   ec.value(),
                   ec.message());
        _socket.close();
        return;
    }

    if (num_read != 8 || memcmp(_rdbuf.data(), "o`P%", 4) != 0)
    {  // validate header chars
        auto ep = _socket.remote_endpoint();
        CPPH_ERROR("{}:{} invalid header received, {} bytes",
                   ep.address().to_string(), ep.port(), num_read);
        _socket.close();
        return;
    }

    auto bufsize = *(int *)(_rdbuf.data() + 4);
    if (bufsize > 128 << 20)
    {
        CPPH_ERROR("requested buffer size {} is too big. aborting connection ...", bufsize);
        _socket.close();
        return;
    }

    _rdbuf.resize(bufsize);
    asio::async_read(
            _socket, _rd(),
            asio::transfer_all(),
            CPPH_BIND(_handle_body));
}

void plain_tcp::_handle_body(asio::error_code const &ec, size_t num_read)
{
    if (ec)
    {  // any error
        auto ep = _socket.remote_endpoint();
        CPPH_ERROR("{}:{} connection aborted: ({}) {}",
                   ep.address().to_string(),
                   ep.port(),
                   ec.value(),
                   ec.message());
        _socket.close();
        return;
    }

    if (num_read != _rdbuf.size())
    {
        CPPH_ERROR("invalid received buffer size {} / requested {}", num_read, _rdbuf.size());
        CPPH_ERROR("aborting connection ...");
        _socket.close();
        return;
    }

    nlohmann::json object;

    std::string_view route;
    nlohmann::json::pointer payload = nullptr;

    try
    {
        auto obj = nlohmann::json::from_msgpack(_rdbuf.begin(), _rdbuf.end());
        route    = obj.at("route").get_ref<std::string &>();
        payload  = &obj.at("payload");
    }
    catch (nlohmann::json::parse_error &e)
    {
        CPPH_ERROR("msgpack parsing error: {}", e.what());
        _socket.close();
        return;
    }
    catch (std::exception &e)
    {
        CPPH_ERROR("error on parsing: {}", e.what());
        _socket.close();
        return;
    }

    received_message_handler(route, *payload);

    _rdbuf.resize(8);
    asio::async_read(
            _socket, _rd(),
            asio::transfer_all(),
            CPPH_BIND(_handle_header));
}
