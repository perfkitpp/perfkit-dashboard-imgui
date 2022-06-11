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

#pragma once

#include <cpph/container/circular_queue.hxx>
#include <cpph/format.hxx>
#include <cpph/functional.hxx>
#include <cpph/macros.hxx>
#include <cpph/utility/ownership.hxx>

#include "TextEditor.h"
#include "classes/session_context.hpp"
#include "session_slot_trace_context.hpp"

class session_slot_close : public std::exception
{
   public:
    session_slot_close(class session_slot* to_close) : close_this(to_close) {}

    session_slot* close_this;
};

class session_slot_invalid_url : public session_slot_close
{
    using session_slot_close::session_slot_close;
};

/**
 * manipulates single session
 *
 */
class session_slot
{
   public:
    enum class state
    {
        disconnected,
        connecting,
        pre_login,
        valid,
    };

    template <typename Ty_>
    struct graph_node
    {
        Ty_ value;
        perfkit::stopwatch timestamp;
    };

    template <typename Ty_>
    using data_footprint = perfkit::circular_queue<graph_node<Ty_>>;

   public:
    explicit session_slot(std::string url, bool from_apiserver, std::string cached_session_name = {});
    ~session_slot();

    std::string const& url() { return _url; }
    bool is_from_apiserver() const { return _from_apiserver; }
    std::string const& latest_session_name() const { return _latest_session_name; }

    /**
     * Performs list label rendering
     *
     * - Render clickable session name label
     *   1. Yet not connected, show connect button. If pressed, start connecting
     *       with spinner icon.
     *   2. If connected, but not login yet, show url with green color,
     *       and on expand, show id/pw enter box and login button.
     *   3. Pressing login button itself does not change UI. It simply submits
     *       login request to remote server, and waits for server's reply.
     *      Thus if user entered invalid id/pw, simply nothing happens.
     *   4. After received 'epoch' message, show session information.
     */
    void render_on_list();

    /**
     * Render terminal window, additionally config/trace/images windows if selected.
     *
     * If terminal window is closed, session slot will move to disconnected state.
     */
    void render_windows();

   private:
    void _title_string();

    template <typename... Args_>
    char const* _key(Args_&&... args)
    {
        return _fmt.format(std::forward<Args_>(args)...).c_str();
    }

    char const* _terminal_window_name()
    {
        return _fmt.format("{}@{} ##TERM:{}", _context->info()->name, _url, _url).c_str();
    }

    void _draw_category_recursive(session_context::config_type const&);
    void _session_state_update(session_context::session_state_type const& state);
    void _plot_on_submenu();

   private:
    // url
    std::string const _url;
    bool _from_apiserver = false;

    std::string _latest_session_name;

    //
    bool _prompt_close = false;
    bool _has_focus    = false;

    // entered id and password, which are cached only for single program instance
    char _id[256] = "guest", _pw[256] = "guest";

    // current state
    state _state = {};

    // connection
    std::unique_ptr<session_context> _context;

    // session state data
    struct _plot_data_t
    {
        data_footprint<float> cpu_total_user{250};
        data_footprint<float> cpu_total_sys{250};
        data_footprint<float> cpu_total{250};
        data_footprint<float> cpu_this_user{250};
        data_footprint<float> cpu_this_sys{250};
        data_footprint<float> cpu_this{250};

        data_footprint<int64_t> mem_virt{250};
        data_footprint<int64_t> mem_rss{250};

        data_footprint<int16_t> num_thrd{500};

        data_footprint<int32_t> bw_out{250};
        data_footprint<int32_t> bw_in{250};
    } _plots;

    //
    perfkit::format_buffer _fmt;
    perfkit::circular_queue<std::string> _history{63};
    perfkit::poll_timer _shello_colorize_timer{1s};
    int64_t _history_cursor = 0;
    int _cmd_prev_cursor    = 0;
    int _shello_color_fence = 0;
    size_t _shello_fence    = 0;
    TextEditor _shello;

    // trace
    perfkit::ownership<session_slot_trace_context> _trace_context;

    // suggestions
    std::future<messages::outgoing::suggest_command> _waiting_suggest;
    std::optional<messages::outgoing::suggest_command> _active_suggest;

    // shell input
    bool _scroll_lock   = false;
    bool _do_autoscroll = false;
    void _draw_shell();
};
