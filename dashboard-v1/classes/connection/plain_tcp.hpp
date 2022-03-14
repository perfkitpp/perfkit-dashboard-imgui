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

#pragma once
#include <asio/ip/tcp.hpp>
#include <perfkit/common/memory/pool.hxx>
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

   private:
    asio::ip::tcp::socket _socket;
    asio::ip::tcp::endpoint _endpoint;

    std::atomic_bool _is_valid = false;

    std::vector<char> _rdbuf;
    perfkit::pool<std::string> _wrpool;

    session_connection_state _status = session_connection_state::invalid;
    nlohmann::json _sendbuf;

    perfkit::logger_ptr _logging = perfkit::share_logger("tcp:plain");
};
