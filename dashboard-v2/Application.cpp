//
// Created by ki608 on 2022-03-14.
//

#include "Application.hpp"

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <imgui.h>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/utility/cleanup.hxx>

#include "utils/Notify.hpp"

Application* Application::Get()
{
    static Application instance;
    return &instance;
}

void Application::TickMainThread()
{
    // Draw dockspace on primary window
    {
        ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.45f, 0.55f, 0.60f, 1.00f));
        ImGui::DockSpaceOverViewport();
        ImGui::PopStyleColor();
    }

    drawMenuContents();

    if (_bDrawSessionList) { drawSessionList(&_bDrawSessionList); }
    if (_bShowMetrics) { ImGui::ShowMetricsWindow(&_bShowMetrics); }
    if (_bShowDemo) { ImGui::ShowDemoWindow(&_bShowDemo); }

    tickSessions();
}

void Application::PostEvent(perfkit::function<void()> callable)
{
    asio::post(*_ioc, std::move(callable));
}

Application::Application()
        : _ioc(std::make_unique<asio::io_context>())
{
}

Application::~Application() = default;

void Application::drawMenuContents()
{
    if (not ImGui::BeginMainMenuBar()) { return; }
    CPPH_CALL_ON_EXIT(ImGui::EndMainMenuBar());

    if (CondInvoke(ImGui::BeginMenu("File"), &ImGui::EndMenu))
    {
        if (ImGui::MenuItem("Save workspace"))
            NotifyToast{}.Severity(NotifySeverity(rand() % (int)NotifySeverity::Fatal + 1)).Title("글쎄요!!").Commit();
        if (ImGui::MenuItem("Save workspace as"))
            NotifyToast{}
                    .AddString("Below is button to press!")
                    .AddButton(
                            [] {
                                NotifyToast{}.AddString("Hello~~").Commit();
                            },
                            "Press Okay")
                    .Commit();
        ImGui::MenuItem("Load workspace");
    }

    if (CondInvoke(ImGui::BeginMenu("View"), &ImGui::EndMenu))
    {
        ImGui::MenuItem("Sessions", "Ctrl+H", &_bDrawSessionList);
    }

    if (CondInvoke(ImGui::BeginMenu("Debug"), &ImGui::EndMenu))
    {
        ImGui::MenuItem("Metrics", NULL, &_bShowMetrics);
        ImGui::MenuItem("Demo", NULL, &_bShowDemo);
    }
}
