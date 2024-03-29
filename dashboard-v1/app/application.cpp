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
#include "application.hpp"

#include <filesystem>
#include <thread>

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <spdlog/spdlog.h>

#include "imgui_extension.h"
#include "imgui.h"
#include "cpphalgorithm/std.hxx"
#include "cpph/utility/cleanup.hxx"
#include "perfkit/configs.h"
#include "perfkit/extension/net.hpp"
#include "perfkit/traces.h"
#include "session_slot.hpp"

#define INTERNAL_CATID_1(A, B) A##B
#define INTERNAL_CATID_0(A, B) INTERNAL_CATID_1(A, B)
#define PUSH_CLEANUP(FN)                      \
    auto INTERNAL_CATID_0(_cleanup, __LINE__) \
    {                                         \
        perfkit::cleanup { &FN }              \
    }

static std::string const CONFIG_PATH = [] {
    return ".perfkit.json";
}();

PERFKIT_CATEGORY(backups)
{
    PERFKIT_CONFIGURE(urls, std::vector<std::tuple<std::string, bool, std::string>>{})
            .confirm();
}

namespace application {
static struct context_t
{
    /**
     * A network dispatcher which uses single dedicated thread.
     */
    asio::io_context ioc_net;

    /**
     * A dedicated thread which manipulates network events
     */
    std::thread worker_net;
    asio::any_io_executor work_net;

    /**
     * A graphics event dispatcher which polls inside main loop.
     */
    asio::io_context ioc_evt;

    /**
     * List of active slots
     */
    std::list<session_slot> sessions;

    /**
     * Local net terminal
     */
    perfkit::terminal_ptr terminal;

    /**
     * Local net tracer
     */
    perfkit::tracer_ptr tracer = perfkit::tracer::create("Loop");
} _context;

void initialize()
{
    perfkit::configs::import_file(CONFIG_PATH);
    backups::update();

    perfkit::terminal::net::terminal_init_info init{"LOCAL"};
    init.serve(49951);
    init.parse_auth("guest:guest:W");
    _context.terminal = perfkit::terminal::net::create(init);

    _context.sessions.emplace_back("127.0.0.1:49951", false, "LOCAL");
    for (auto& [url, is_relay, cached_name] : backups::urls.ref())
    {
        if (url == "127.0.0.1:49951")
            continue;

        _context.sessions.emplace_back(url, is_relay, cached_name);
    }

    _context.work_net
            = asio::require(
                    _context.ioc_net.get_executor(),
                    asio::execution::outstanding_work.tracked);
    ;
    _context.worker_net = std::thread{
            [] {
                _context.ioc_net.run();
            }};
}

void shutdown()
{
    // Validate configuration file
    gui::_refresh_session_list_backup();

    // Expire net provider
    _context.terminal.reset();

    // Expire tracer
    _context.tracer.reset();

    // shutdown dedicated network thread
    _context.sessions.clear();

    _context.work_net = {};
    _context.worker_net.join();

    // export backups
    perfkit::configs::export_to(CONFIG_PATH);
}

void update()
{
    PERFKIT_TRACE(_context.tracer);

    PERFKIT_TRACE_SEQUENCE(RunIoContext);
    _context.ioc_evt.run();
    _context.ioc_evt.restart();

    PERFKIT_TRACE_SEQUENCE(DrawRootComponents);
    gui::_draw_root_components();

    PERFKIT_TRACE_SEQUENCE(DrawAllSessions);
    gui::_draw_session_list();

    PERFKIT_TRACE_SEQUENCE(RenderRemoteGraphics);
    gui::_render_windows();
}

void push_message_0(message_level level, std::string content)
{
    // TODO
}

asio::io_context& ioc_net()
{
    return _context.ioc_net;
}

void post_event(perfkit::ufunction<void()> evt)
{
    asio::post(_context.ioc_evt, std::move(evt));
}

void gui::_draw_root_components()
{
    using namespace ImGui;

    static bool _show_demo          = false;
    static bool _show_metrics       = false;
    static bool _show_connect_indep = false;
    static bool _show_connect_relay = false;

    if (BeginMainMenuBar())
    {  // draw menu
        if (BeginMenu("Files"))
        {
            if (MenuItem("Save", "Ctrl+S"))
            {
                perfkit::configs::export_to(CONFIG_PATH);
            }
            if (MenuItem("Reload"))
            {
                perfkit::configs::import_file(CONFIG_PATH);
                backups::update();
            }

            ImGui::EndMenu();
        }

        if (BeginMenu("Debugging"))
        {
            Checkbox("Show Demo Window ...", &_show_demo);
            Checkbox("Show Metrics ...", &_show_metrics);

            ImGui::EndMenu();
        }

        if (BeginMenu("View"))
        {
            MenuItem("Session List", "Alt+1");

            ImGui::EndMenu();
        }

        EndMainMenuBar();
    }

    ImGui::DockSpaceOverViewport();

    if (_show_demo)
        ShowDemoWindow(&_show_demo);

    if (_show_metrics)
        ShowMetricsWindow(&_show_metrics);

    if (_show_connect_indep)
        detail::modal_single_server_connect(&_show_connect_indep);

    if (_show_connect_relay)
        detail::modal_api_server_connect(&_show_connect_relay);
}

void gui::_draw_session_list()
{
    auto& sessions = _context.sessions;

    if (ImGui::Begin("Sessions"))
    {
        if (ImGui::TreeNodeEx("Add New ...", ImGuiTreeNodeFlags_SpanFullWidth))
        {
            static char buf_url[1024] = {};
            ImGui::InputTextLeft("URL", "localhost:5572", buf_url, sizeof buf_url);

            if (ImGui::Button("Direct Connect", {-1, 0}))
            {
                // create new connection
                std::string url = buf_url;

                auto is_unique = perfkit::none_of(
                        _context.sessions,
                        [&](auto&& sess) {
                            return sess.url() == buf_url;
                        });

                if (not url.empty() && is_unique)
                {
                    SPDLOG_INFO("new connection candidate {} added.", url);
                    push_message(message_level::info, "new connection candidate {} added.", url);
                    _context.sessions.emplace_back(std::move(url), false);

                    _refresh_session_list_backup();
                }
            }

            ImGui::Button("Thru Relay Server", {-1, 0});
            ImGui::TreePop();

            ImGui::Separator();
        }

        if (ImGui::TreeNodeEx("Sessions", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth))
        {
            for (auto it = sessions.begin(); it != sessions.end();)
            {
                try
                {
                    it->render_on_list();
                    ++it;
                }
                catch (session_slot_close&)
                {
                    it = sessions.erase(it);
                    _refresh_session_list_backup();
                }
            }

            ImGui::TreePop();
        }
    }
    ImGui::End();

    for (auto it = sessions.begin(); it != sessions.end();)
    {
        try
        {
            it->render_windows();
            ++it;
        }
        catch (session_slot_close&)
        {
            it = sessions.erase(it);
        }
    }
}

void gui::_render_windows()
{
    // 3D rendering
}

void gui::_refresh_session_list_backup()
{
    // backup list
    decltype(backups::urls.value()) tups;
    tups.reserve(_context.sessions.size());

    for (auto& sess : _context.sessions)
    {
        tups.emplace_back(sess.url(), sess.is_from_apiserver(), sess.latest_session_name());
    }

    backups::urls.async_modify(tups);
}

void gui::detail::modal_single_server_connect(bool* connStat)
{
    ImGui::Begin("Connect to Instance", connStat);

    ImGui::Text("Enter target server information");

    static char buf_url[1024] = {};
    ImGui::InputText("URL", buf_url, sizeof buf_url);

    if (ImGui::Button("Submit", {-1, 0}))
    {
        // create new connection
        std::string url = buf_url;

        auto is_unique = perfkit::none_of(
                _context.sessions,
                [&](auto&& sess) {
                    return sess.url() == buf_url;
                });

        if (not url.empty() && is_unique)
        {
            SPDLOG_INFO("new connection candidate {} added.", url);
            _context.sessions.emplace_back(std::move(url), false);
        }

        *connStat = false;
    }
    ImGui::End();
}

void gui::detail::modal_api_server_connect(bool* connStat)
{
}
}  // namespace applic