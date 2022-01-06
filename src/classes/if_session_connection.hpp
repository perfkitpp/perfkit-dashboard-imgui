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
