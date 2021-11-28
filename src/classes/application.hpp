//
// Created by ki608 on 2021-11-28.
//

#pragma once
#include <asio/io_context.hpp>

namespace application
{
void initialize();
void update();
}  // namespace application

namespace application::gui
{
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
}  // namespace application::gui
