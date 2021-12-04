#pragma once

#include <perfkit/common/functional.hxx>
#include <perfkit/common/macros.hxx>

#include "classes/session_context.hpp"

/**
 * manipulates single session
 *
 */
class session_slot
{
   private:
    enum class state
    {
        invalid,
        connecting,
        pre_login,
        valid,
    };

   public:
    explicit session_slot(std::string url);

    std::string const& url() { return _url; }

    /**
     * performs list label rendering
     *
     * - render clickable session name label
     *   1. yet not connected, show url with spinner
     *   2. if connected, but not login yet, show url with green color
     *       and if selected, 
     *
     * \return true if selected
     */
    bool render_on_list();

    /**
     * 
     */
    void render_defaults();

    /**
     * render 
     */
    void render_on_select();

   private:
    std::string _url;
};
