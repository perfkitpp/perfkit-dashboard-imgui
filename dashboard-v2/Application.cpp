//
// Created by ki608 on 2022-03-14.
//

#include "Application.hpp"

#include <thread>

#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <perfkit/common/algorithm/std.hxx>
#include <perfkit/common/helper/nlohmann_json_macros.hxx>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/utility/cleanup.hxx>
#include <perfkit/configs.h>

#include "interfaces/Session.hpp"

PERFKIT_CATEGORY(GConfig::Workspace)
{
    struct SessionArchive
    {
        string Key;
        int Type = 0;
        string DisplayName;

        CPPHEADERS_DEFINE_NLOHMANN_JSON_ARCHIVER(SessionArchive, Key, Type, DisplayName);
    };

    PERFKIT_CONFIGURE(ArchivedSessions, vector<SessionArchive>{}).confirm();
}

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
    ImGuiSettingsHandler ini_handler;
    ini_handler.TypeName = "DashboardV2";
    ini_handler.TypeHash = ImHashStr(ini_handler.TypeName);
    ini_handler.UserData = this;
    ini_handler.ReadOpenFn
            = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) {
                  if (strcmp(name, "ConfPaths") == 0)
                      return (void*)1;
                  else
                      return (void*)nullptr;
              };

    ini_handler.ReadLineFn
            = [](ImGuiContext*, ImGuiSettingsHandler* h, void* p, const char* data) {
                  if (not p) { return; }

                  auto self = (Application*)h->UserData;
                  if (strncmp(data, "WorkspaceFile", strlen("WorkspaceFile")) == 0)
                  {
                      self->_workspacePath.resize(1024);
                      sscanf(data, "WorkspaceFile=%s", self->_workspacePath.data());
                      self->_workspacePath.resize(strlen(self->_workspacePath.c_str()));

                      self->loadWorkspace();
                  }
              };

    ini_handler.WriteAllFn
            = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* buf) {
                  auto self = (Application*)h->UserData;
                  if (self->_workspacePath.empty())
                      self->_workspacePath = "perfkit-workspace.json";

                  buf->appendf("[%s][%s]\n", h->TypeName, "ConfPaths");
                  buf->appendf("WorkspaceFile=%s\n", self->_workspacePath.c_str());
                  buf->append("\n");

                  self->saveWorkspace();
              };

    ImGui::GetCurrentContext()->SettingsHandlers.push_back(ini_handler);
}

Application::~Application() = default;

void Application::drawMenuContents()
{
    if (not ImGui::BeginMainMenuBar()) { return; }
    CPPH_CALL_ON_EXIT(ImGui::EndMainMenuBar());

    if (CondInvoke(ImGui::BeginMenu("File"), &ImGui::EndMenu))
    {
        if (ImGui::MenuItem("Save workspace"))
            saveWorkspace();
        if (ImGui::MenuItem("Save workspace as"))
            ;
        if (ImGui::MenuItem("Load workspace"))
            ;
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

        auto baseColor       = bIsSessionOpen ? 0xff'264d22 : 0xff'282828;
        baseColor            = sess.bPendingClose ? 0xff'37b8db : baseColor;
        sess.bPendingClose   = sess.bPendingClose && bIsSessionOpen;
        bool bRenderContents = sess.Ref->ShouldRenderSessionListEntityContent();

        if (not bRenderContents)
        {
            headerFlag |= ImGuiTreeNodeFlags_Bullet;
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

        bRenderContents &= ImGui::CollapsingHeader(textBuf, &bOpenStatus, headerFlag);
        ImGui::SameLine();
        ImGui::TextColored({.5f, .5f, .5f, 1.f}, "%s", sess.Key.c_str());

        if (bIsSessionOpen)
        {
            ImGui::SameLine();

            if (sess.bShow)
                sprintf(textBuf, "[HIDE]###OPN-%s-%d", sess.Key.c_str(), sess.Type);
            else
                sprintf(textBuf, "[SHOW]###OPN-%s-%d", sess.Key.c_str(), sess.Type);

            int buttonBase = sess.bShow ? 0xff'6ccf48 : baseColor;

            CPPH_CALL_ON_EXIT(ImGui::PopStyleColor(4));
            ImGui::PushStyleColor(ImGuiCol_Button, buttonBase);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonBase + offsetActive);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonBase + offsetHover);
            ImGui::PushStyleColor(ImGuiCol_Text, 0xffcccccc);

            if (ImGui::Button(textBuf, {-18, 0}))
                sess.bShow = not sess.bShow, NotifyToast{}.String("Toggle!");
        }

        if (bRenderContents)
        {
            sprintf(textBuf, "%s##CHLD-%s-%d", sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);

            ImGui::TreePush();
            sess.Ref->RenderSessionListEntityContent();
            ImGui::TreePop();

            ImGui::Separator();
        }

        sprintf(textBuf, "Unregister##%s-%d", sess.Key.c_str(), sess.Type);
        if (not bOpenStatus)
        {
            if (bIsSessionOpen && not sess.bPendingClose)
            {
                // If session was originally open, try close session.
                sess.Ref->CloseSession();
                sess.bPendingClose = true;
            }
            else
            {
                // Otherwise, popup modal for deleting this session
                ImGui::OpenPopup(textBuf);
            }
        }

        if (CondInvoke(ImGui::BeginPopup(textBuf), ImGui::EndPopup))
        {
            sess.bPendingClose = false;

            ImGui::Text("Are you sure to unregister this session?");
            if (ImGui::Button("Yes"))
            {
                iter = _sessions.erase(iter);
                ImGui::CloseCurrentPopup();
                ImGui::MarkIniSettingsDirty();
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
        ImGui::MarkIniSettingsDirty();
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
                .String("Session key {} already exist", keyString);
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
                .String("[URI {}]: Given session type is not implemented yet ...", keyString);
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

    NotifyToast{}.String("Session {}@{} Created", elem->CachedDisplayName, elem->Key);
    return true;
}

bool Application::isSessionExist(std::string_view name, ESessionType type)
{
    using namespace perfkit::algorithm;

    auto predFindDup = [&](auto&& elem) { return elem.Key == name && elem.Type == type; };
    return (_sessions.end() != find_if(_sessions, predFindDup));
}

void Application::loadWorkspace()
{
    if (not perfkit::configs::import_file(_workspacePath))
    {
        NotifyToast{}.Error().String("Config path '{}' is not a valid file.", _workspacePath);
        return;
    }

    GConfig::Workspace::update();

    // Load Sessions
    {
        _sessions.clear();
        for (auto& desc : *GConfig::Workspace::ArchivedSessions)
            RegisterSessionMainThread(desc.Key, ESessionType(desc.Type), desc.DisplayName);
        NotifyToast{}.String("{} sessions loaded", _sessions.size());
    }
}

void Application::saveWorkspace()
{
    // Export Sessions
    {
        std::vector<GConfig::Workspace::SessionArchive> archive;
        archive.reserve(_sessions.size());

        for (auto& sess : _sessions)
        {
            auto arch         = &archive.emplace_back();
            arch->Key         = sess.Key;
            arch->DisplayName = sess.CachedDisplayName;
            arch->Type        = int(sess.Type);
        }

        GConfig::Workspace::ArchivedSessions.commit(std::move(archive));
        NotifyToast{}.Trivial().String("{} sessions exported to {}", _sessions.size(), _workspacePath);
    }

    GConfig::Workspace::update();
    perfkit::configs::export_to(_workspacePath);
}
