#pragma once

#include <perfkit/common/circular_queue.hxx>
#include <perfkit/common/format.hxx>
#include <perfkit/common/functional.hxx>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/utility/ownership.hxx>

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
   private:
    enum class state
    {
        disconnected,
        connecting,
        pre_login,
        valid,
    };

   public:
    explicit session_slot(std::string url, bool from_apiserver);
    ~session_slot();

    std::string const& url() { return _url; }
    bool is_from_apiserver() const { return _from_apiserver; }

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

   private:
    // url
    std::string const _url;
    bool _from_apiserver = false;

    //
    bool _prompt_close = false;
    bool _has_focus    = false;

    // entered id and password, which are cached only for single program instance
    char _id[256] = {}, _pw[256] = {};

    // current state
    state _state = {};

    // connection
    std::unique_ptr<session_context> _context;

    //
    perfkit::format_buffer _fmt;
    perfkit::circular_queue<std::string> _history{63};
    int64_t _history_cursor = 0;
    int _cmd_prev_cursor    = 0;
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
