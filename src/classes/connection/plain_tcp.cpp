// MIT License
//
// Copyright (c) 2022. Seungwoo Kang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// project home: https://github.com/perfkitpp

//
// Created by ki608 on 2021-11-28.
//

#include "plain_tcp.hpp"

#include <thread>

#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <perfkit/common/format.hxx>
#include <perfkit/common/macros.hxx>
#include <spdlog/spdlog.h>

using namespace perfkit::literals;

void plain_tcp::send_message(std::string_view route, const nlohmann::json &parameter)
{
    _sendbuf["route"]     = route;
    _sendbuf["parameter"] = parameter;

    auto buffer = _wrpool.checkout();
    buffer->clear();
    buffer->resize(8);
    nlohmann::json::to_msgpack(_sendbuf, {*buffer});

    memcpy(buffer->data(), "o`P%", 4);
    *(int *)&(*buffer)[4] = (int)buffer->size() - 8;

    auto pbuf = &*buffer;
    asio::async_write(_socket,
                      asio::buffer(*pbuf),
                      asio::transfer_all(),
                      [buffer = std::move(buffer)](auto &&ec, size_t n) {});
}

session_connection_state plain_tcp::status() const
{
    if (not _socket.is_open())
        return session_connection_state::invalid;

    return _status;
}

plain_tcp::plain_tcp(asio::io_context *ioc, const char *address, uint16_t port)
        : _socket{*ioc}
{
    asio::ip::tcp::resolver resolver{*ioc};

    auto iter = resolver.resolve(address, std::to_string(port));
    if (not iter.empty())
    {
        // auto ep = asio::ip::tcp::endpoint{asio::ip::make_address(address), port};
        auto ep   = (*iter.begin()).endpoint();
        _endpoint = ep;
        _socket.open(ep.protocol());

        _socket.async_connect(ep, CPPH_BIND(_on_connect));
        _status = session_connection_state::connecting;
    }
}

plain_tcp::~plain_tcp()
{
    if (_socket.is_open())
    {
        try
        {
            _socket.shutdown(asio::socket_base::shutdown_both);
            _socket.close();
        }
        catch (asio::system_error &e)
        {
            CPPH_ERROR("Socket shutdown() ~ close() failed: ({}) {}", e.code().value(), e.what());
        }
    }
}

void plain_tcp::_on_connect(const asio::error_code &ec)
{
    if (ec)
    {
        auto ep = _endpoint;
        CPPH_ERROR("{}:{} connection aborted: ({}) {}",
                   ep.address().to_string(),
                   ep.port(),
                   ec.value(),
                   ec.message());

        _socket.close();
        return;
    }

    _rdbuf.resize(8);
    _is_valid = true;

    _status = session_connection_state::connected;
    asio::async_read(
            _socket, _rd(),
            asio::transfer_all(),
            CPPH_BIND(_handle_header));
}

void plain_tcp::_handle_header(asio::error_code const &ec, size_t num_read)
{
    if (ec && ec.value() == asio::error::operation_aborted)
    {
        return;
    }

    if (ec)
    {  // any error
        auto ep = _endpoint;
        CPPH_ERROR("{}:{} connection aborted: ({}) {}",
                   ep.address().to_string(),
                   ep.port(),
                   ec.value(),
                   ec.message());
        try
        {
            _socket.close();
        }
        catch (asio::system_error &e)
        {
            CPPH_ERROR("Failed to close socket: ({}) {}", e.code().value(), e.what());
        }
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
    if (ec && ec.value() == asio::error::operation_aborted)
    {
        return;
    }

    if (ec)
    {  // any error
        auto ep = _endpoint;
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
        object  = nlohmann::json::from_msgpack(_rdbuf.begin(), _rdbuf.end());
        route   = object.at("route").get_ref<std::string &>();
        payload = &object.at("payload");
    }
    catch (nlohmann::json::parse_error &e)
    {
        CPPH_ERROR("msgpack parsing error: {}, data size was: {}", e.what(), _rdbuf.size());
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
