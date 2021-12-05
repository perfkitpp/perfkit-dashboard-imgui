//
// Created by ki608 on 2021-11-28.
//

#pragma once
#include <perfkit/common/functional.hxx>

namespace asio {
class io_context;
}

namespace application {
void initialize();
void shutdown();
void update();

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
