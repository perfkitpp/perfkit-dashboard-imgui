//
// Created by ki608 on 2021-11-28.
//

#include "application.hpp"

#include <thread>

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <spdlog/spdlog.h>

#include "imgui.h"
#include "perfkit/common/algorithm.hxx"
#include "perfkit/common/utility/cleanup.hxx"
#include "session_slot.hpp"

#define INTERNAL_CATID_1(A, B) A##B
#define INTERNAL_CATID_0(A, B) INTERNAL_CATID_1(A, B)
#define PUSH_CLEANUP(FN)                      \
    auto INTERNAL_CATID_0(_cleanup, __LINE__) \
    {                                         \
        perfkit::cleanup { &FN }              \
    }

namespace application
{
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
     * TODO: list of active application server slots
     */
} _context;

void initialize()
{
    _context.work_net = asio::require(
            _context.ioc_net.get_executor(),
            asio::execution::outstanding_work.tracked);
    ;
    _context.worker_net = std::thread{
            []
            {
                _context.ioc_net.run();
            }};
}

void shutdown()
{
    // shutdown dedicated network thread
    _context.work_net = {};
    _context.worker_net.join();
}

void update()
{
    // poll event loop once.
    _context.ioc_evt.run();
    _context.ioc_evt.restart();

    // render GUI
    gui::_draw_root_components();
    gui::_draw_session_list();

    gui::_render_windows();
}

void post_event(perfkit::function<void()> evt)
{
    asio::post(std::move(evt));
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
            if (BeginMenu("Connect To"))
            {
                _show_connect_indep |= MenuItem("Instance");
                _show_connect_relay |= MenuItem("Relay Server");
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (BeginMenu("Debugging"))
        {
            Checkbox("Show Demo Window ...", &_show_demo);
            Checkbox("Show Metrics ...", &_show_metrics);

            ImGui::EndMenu();
        }

        EndMainMenuBar();
    }

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
}

void gui::_render_windows()
{
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
                [&](auto&& sess)
                {
                    return sess.url() == buf_url;
                });

        if (not url.empty() && is_unique)
        {
            SPDLOG_INFO("new connection candidate {} added.", url);
            _context.sessions.emplace_back(std::move(url));
        }

        *connStat = false;
    }
    ImGui::End();
}

void gui::detail::modal_api_server_connect(bool* connStat)
{
}
}  // namespace application