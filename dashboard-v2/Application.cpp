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
                            "Press Okay");
        ImGui::MenuItem("Load workspace");
    }

    if (CondInvoke(ImGui::BeginMenu("View"), &ImGui::EndMenu))
    {
        ImGui::MenuItem("Sessions", "Ctrl+H", &_bDrawSessionList);

        ImGui::Separator();
        ImGui::TextColored({.5f, .5f, .5f, 1.f}, "Debugging");

        ImGui::MenuItem("Metrics", NULL, &_bShowMetrics);
        ImGui::MenuItem("Demo", NULL, &_bShowDemo);
    }

    if (CondInvoke(ImGui::BeginMenu("Add"), &ImGui::EndMenu))
        if (CondInvoke(ImGui::BeginMenu("Session..."), &ImGui::EndMenu))
            drawAddSessionMenu();
}

void Application::drawSessionList(bool* bKeepOpen)
{
    CPPH_CALL_ON_EXIT(ImGui::End());
    if (not ImGui::Begin("Sessions", bKeepOpen, ImGuiWindowFlags_MenuBar)) { return; }

    if (CondInvoke(ImGui::BeginMenuBar(), &ImGui::EndMenuBar))
        if (CondInvoke(ImGui::BeginMenu("Add Session"), &ImGui::EndMenu))
            drawAddSessionMenu();

    char textBuf[256];
    auto const colorBase    = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Header));
    auto const offsetActive = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)) - colorBase;
    auto const offsetHover  = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)) - colorBase;

    for (auto iter = _sessions.begin(); iter != _sessions.end();)
    {
        auto& sess = *iter;
        sess.Ref->FetchSessionDisplayName(&sess.CachedDisplayName);

        bool const bIsSessionOpen = sess.Ref->IsSessionOpen();
        bool bOpenStatus          = true;
        auto headerFlag           = 0;
        int colorPopCount         = 3;
        CPPH_CALL_ON_EXIT(ImGui::PopStyleColor(colorPopCount));

        auto baseColor = bIsSessionOpen ? 0xff'264d22 : 0xff'383838;

        if (not sess.Ref->ShouldRenderSessionListEntityContent())
        {
            headerFlag |= ImGuiTreeNodeFlags_Leaf;
            ImGui::PushStyleColor(ImGuiCol_Header, baseColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, baseColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, baseColor);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Header, baseColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, baseColor + offsetActive);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, baseColor + offsetHover);
        }

        sprintf(textBuf, "%s##SLB-%s-%d", sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);
        if (ImGui::CollapsingHeader(textBuf, &bOpenStatus, headerFlag))
        {
            sprintf(textBuf, "%s##CHLD-%s-%d", sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);

            ImGui::TreePush();
            sess.Ref->RenderSessionListEntityContent();
            ImGui::TreePop();
        }

        ImGui::SameLine();
        ImGui::TextColored({.5f, .5f, .5f, 1.f}, "%s", sess.Key.c_str());

        sprintf(textBuf, "Unregister##%s-%d", sess.Key.c_str(), sess.Type);
        if (not bOpenStatus)
        {
            if (bIsSessionOpen)
            {
                // If session was originally open, try close session.
                sess.Ref->CloseSession();
            }
            else
            {
                // Otherwise, popup modal for deleting this session
                ImGui::OpenPopup(textBuf);
            }
        }

        if (CondInvoke(ImGui::BeginPopup(textBuf), ImGui::EndPopup))
        {
            ImGui::Text("Are you sure to unregister this session?");
            if (ImGui::Button("Yes"))
            {
                iter = _sessions.erase(iter);
                ImGui::CloseCurrentPopup();
                continue;
            }

            ImGui::SameLine();
            if (ImGui::Button("No"))
            {
                ImGui::CloseCurrentPopup();
            }
        }

        ++iter;
    }
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

shared_ptr<ISession> CreatePerfkitTcpRawClient();

bool Application::RegisterSessionMainThread(
        string keyString, ESessionType type, string_view optionalDefaultDisplayName)
{
    if (isSessionExist(keyString, type))
    {
        NotifyToast{}
                .Severity(NotifySeverity::Error)
                .Title("Session Creation Failed")
                .AddString("Session key {} already exist", keyString);
        return false;
    }

    shared_ptr<ISession> session;

    switch (type)
    {
        case ESessionType::TcpRawClient:
            session = CreatePerfkitTcpRawClient();
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
                .AddString("[URI {}]: Given session type is not implemented yet ...", keyString);
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

    NotifyToast{}.AddString("Session {}@{} Created", elem->CachedDisplayName, elem->Key);
    return true;
}

bool Application::isSessionExist(std::string_view name, ESessionType type)
{
    using namespace perfkit::algorithm;

    auto predFindDup = [&](auto&& elem) { return elem.Key == name && elem.Type == type; };
    return (_sessions.end() != find_if(_sessions, predFindDup));
}
