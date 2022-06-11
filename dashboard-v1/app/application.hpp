// MIT License
//
// Copyright (c) 2021-2022. Seungwoo Kang
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
#include <cpph/utility/functional.hxx>
#include <spdlog/fmt/bundled/format.h>

namespace asio {
class io_context;
}

enum class message_level
{
    verbose,
    debug,
    info,
    warning,
    error,
    fatal
};

namespace application {

void initialize();
void shutdown();
void update();

void push_message_0(message_level level, std::string content);

template <typename Str_, typename... Args_>
void push_message(message_level level, Str_ format, Args_&&... args)
{
    push_message_0(level, fmt::format(format, std::forward<Args_>(args)...));
}

asio::io_context& ioc_net();

void post_event(perfkit::function<void()> evt);

}  // namespace application

namespace application::gui {
/**
 * Render ImGui windows
 */
void _draw_root_components();

/**
 * - List up sessions / API servers
 * - Provides detailed information for each sessions
 */
void _draw_session_list();

/**
 * Render custom draw requests via OpenGL, handle resource upload requests, etc.
 */
void _render_windows();

/**
 * Refresh sessions configs
 */
void _refresh_session_list_backup();

namespace detail {
void modal_single_server_connect(bool* connStat);
void modal_api_server_connect(bool* connStat);

}  // namespace detail
}  // namespace application::gui
