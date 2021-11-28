//
// Created by ki608 on 2021-11-28.
//

#include "application.hpp"

#include "imgui.h"
#include "perfkit/common/utility/cleanup.hxx"

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
     * A graphics event dispatcher which polls inside main loop.
     */
    asio::io_context ioc_graphic;
} _context;

void initialize()
{
}

void update()
{
    gui::_draw_root_components();
    gui::_render_windows();
}

void gui::_draw_root_components()
{
    using namespace ImGui;

    static bool _show_demo    = false;
    static bool _show_metrics = false;

    if (BeginMainMenuBar())
    {  // draw menu
        if (BeginMenu("Files"))
        {
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
}

void gui::_render_windows()
{
}
}  // namespace application