#pragma once

#include <perfkit/common/format.hxx>
#include <perfkit/common/functional.hxx>
#include <perfkit/common/macros.hxx>

#include "classes/session_context.hpp"

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
     *
     * \return true if selected
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

   private:
    // url
    std::string _url;

    //
    bool _from_apiserver = false;

    //
    bool _prompt_close = false;

    // entered id and password, which are cached only for single program instance
    char _id[256] = {}, _pw[256] = {};

    // current state
    state _state = {};

    // connection
    std::unique_ptr<session_context> _context;

    //
    perfkit::format_buffer _fmt;
};
