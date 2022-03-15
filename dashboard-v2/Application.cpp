//
// Created by ki608 on 2022-03-14.
//

#include "Application.hpp"

#include <thread>

#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <imgui.h>
#include <perfkit/common/algorithm/std.hxx>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/utility/cleanup.hxx>

#include "interfaces/Session.hpp"
#include "utils/Notify.hpp"

Application* Application::Get()
{
    static Application instance;
    return &instance;
}

void Application::TickMainThread()
{
    // Process events
    {
        _ioc->run(), _ioc->restart();
    }

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

void Application::PostMainThreadEvent(perfkit::function<void()> callable)
{
    asio::post(*_ioc, std::move(callable));
}

void Application::DispatchMainThreadEvent(perfkit::function<void()> callable)
{
    asio::dispatch(*_ioc, std::move(callable));
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
                            [] { NotifyToast{}.AddString("Hello~~").Commit(); },
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

    if (CondInvoke(ImGui::BeginMenu("Add Session"), &ImGui::EndMenu))
    {
        drawAddSessionMenu();
    }
}

void Application::drawSessionList(bool* bKeepOpen)
{
    CPPH_CALL_ON_EXIT(ImGui::End());
    if (not ImGui::Begin("Sessions", bKeepOpen, ImGuiWindowFlags_MenuBar)) { return; }

    if (CondInvoke(ImGui::BeginMenuBar(), &ImGui::EndMenuBar))
        if (CondInvoke(ImGui::BeginMenu("Add Session"), &ImGui::EndMenu))
            drawAddSessionMenu();
}

void Application::drawAddSessionMenu()
{
    constexpr char const* ItemNames[] = {"-- NONE --", "Tcp Raw Client", "WebSocket Client"};
    auto state                        = &_addSessionModalState;

    static_assert(std::size(ItemNames) == int(ESessionType::ENUM_MAX_VALUE));

    if (CondInvoke(ImGui::BeginCombo("Session Type", ItemNames[int(state->Selected)]), &ImGui::EndCombo))
    {
        for (int i = 1; i < int(ESessionType::ENUM_MAX_VALUE); ++i)
        {
            bool bIsSelected = (i == int(state->Selected));
            if (ImGui::Selectable(ItemNames[i], bIsSelected))
            {
                state->Selected             = ESessionType(i);
                state->bActivateButton      = 0 < strlen(state->UriBuffer);
                state->bSetNextFocusToInput = true;
            }

            if (bIsSelected)
                ImGui::SetItemDefaultFocus();
        }
    }

    if (state->Selected == ESessionType::None) { return; }

    // Draw URI input box
    if (state->bSetNextFocusToInput)
    {
        state->bSetNextFocusToInput = false;
        ImGui::SetKeyboardFocusHere();
    }

    if (ImGui::InputText("URI", state->UriBuffer, sizeof state->UriBuffer, ImGuiInputTextFlags_AutoSelectAll))
    {
        std::string_view uri = state->UriBuffer;

        if (uri.empty() || isSessionExist(uri, state->Selected))
        {
            state->bActivateButton = false;
        }
        else
        {
            state->bActivateButton = true;
        }
    }

    // Expose add session button only when conditions are valid
    if (state->bActivateButton && (ImGui::Button("Apply", {-1, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
    {
        RegisterSessionMainThread(state->UriBuffer, state->Selected);
        state->bActivateButton      = false;
        state->bSetNextFocusToInput = true;
    }
}

bool Application::RegisterSessionMainThread(
        string keyString, ESessionType type, string_view optionalDefaultDisplayName)
{
    if (isSessionExist(keyString, type))
    {
        NotifyToast{}
                .Severity(NotifySeverity::Error)
                .Title("Session Creation Failed")
                .AddString("Session key {} already exist", keyString)
                .Commit();
        return false;
    }

    shared_ptr<ISession> session;

    switch (type)
    {
        case ESessionType::TcpRawClient:
            // TODO: create TCP RAW client
            break;

        case ESessionType::WebSocketClient:
            // TODO: create web socket client
            break;

        default:
            break;
    }

    if (not session)
    {
        NotifyToast{}
                .Severity(NotifySeverity::Error)
                .Title("Session Creation Failed")
                .AddString("Seesion type {} is not implemented yet ...", (int)type)
                .Commit();
        return false;
    }

    auto elem               = &_sessions.emplace_back();
    elem->Key               = std::move(keyString);
    elem->CachedDisplayName = optionalDefaultDisplayName;
    elem->Type              = type;
    elem->Ref               = std::move(session);
    elem->bShow             = false;

    elem->Ref->InitializeSession(elem->Key);
    elem->Ref->FetchSessionDisplayName(&elem->CachedDisplayName);

    NotifyToast{}.AddString("Session {} Created", elem->Key).Commit();
    return true;
}

bool Application::isSessionExist(std::string_view name, ESessionType type)
{
    using namespace perfkit::algorithm;

    auto predFindDup = [&](auto&& elem) { return elem.Key == name && elem.Type == type; };
    return (_sessions.end() != find_if(_sessions, predFindDup));
}
