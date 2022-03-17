//
// Created by ki608 on 2022-03-14.
//

#include "Application.hpp"

#include <thread>

#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <imgui.h>
#include <imgui_extension.h>
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
        int    Type = 0;
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

    // Tick graphics
    tickGraphicsMainThread();
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
            NotifyToast{}.Fatal().String("Not Implemented: " __FILE__ ":{}", __LINE__);
        if (ImGui::MenuItem("Load workspace"))
            NotifyToast{}.Fatal().String("Not Implemented: " __FILE__ ":{}", __LINE__);
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
    if (not ImGui::Begin("Sessions", bKeepOpen)) { return; }

    ImGui::AlignTextToFramePadding(), ImGui::Text("Add Session");

    if (auto _scope_ = ImGui::ChildWindowGuard("Session-AddNew"))
        drawAddSessionMenu();

    ImGui::AlignTextToFramePadding(), ImGui::Text("Sessions");
    CPPH_CALL_ON_EXIT(ImGui::EndChild());
    ImGui::BeginChild("Session-List", {0, 0}, true);

    char       textBuf[256];
    auto const colorBase    = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Header));
    auto const offsetActive = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive)) - colorBase;
    auto const offsetHover  = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)) - colorBase;

    for (auto iter = _sessions.begin(); iter != _sessions.end();)
    {
        auto& sess = *iter;
        sess.Ref->FetchSessionDisplayName(&sess.CachedDisplayName);

        bool const bIsSessionOpen = sess.Ref->IsSessionOpen();
        bool       bOpenStatus    = true;
        auto       headerFlag     = 0;
        int        colorPopCount  = 3;
        CPPH_CALL_ON_EXIT(ImGui::PopStyleColor(colorPopCount));

        auto baseColor       = bIsSessionOpen ? 0xff'113d16 : 0xff'080808;
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

        sprintf(textBuf, "%s###SLB-%s-%d",
                sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);

        ImGui::PushStyleColor(ImGuiCol_Text, bIsSessionOpen ? 0xffffffff : 0xffbbbbbb);
        bRenderContents &= ImGui::CollapsingHeader(textBuf, &bOpenStatus, headerFlag);
        ImGui::PopStyleColor();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { sess.bShow = not sess.bShow; }

        if (bIsSessionOpen)
        {
            ImGui::SameLine();
            ImGui::TextColored({0, 1, 0, 1}, " [%c]", "-\\|/"[int(ImGui::GetTime() / 0.33) % 4]);
        }

        if (sess.bShow)
        {
            ImGui::SameLine();
            ImGui::TextColored({1, 1, 0, 1}, "[*]");
        }

        ImGui::SameLine();
        ImGui::TextColored({.5f, .5f, .5f, 1.f}, "%s", sess.Key.c_str());

        if (bRenderContents)
        {
            sprintf(textBuf, "%s##CHLD-%s-%d", sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg) - ImVec4{.1, .1, .1, .0});
            CPPH_CALL_ON_EXIT(ImGui::PopStyleColor());

            CPPH_CALL_ON_EXIT(ImGui::EndChild());
            if (ImGui::BeginChild(textBuf, {0, 250}, true))
            {
                sess.Ref->RenderSessionListEntityContent();
            }
        }

        sprintf(textBuf, "Unregister##%s-%d", sess.Key.c_str(), sess.Type);
        if (not bOpenStatus)
        {
            if (bIsSessionOpen && not sess.bPendingClose)
            {
                // If session was originally open, try close session.
                sess.Ref->CloseSession();
                sess.bPendingClose = true;

                NotifyToast("Session Closed").String("{}@{}", sess.CachedDisplayName, sess.Key);
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
                auto name = iter->Key;

                iter      = _sessions.erase(iter);
                ImGui::CloseCurrentPopup();
                ImGui::MarkIniSettingsDirty();

                NotifyToast("Session Erased").String(name);
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
    auto                  state       = &_addSessionModalState;

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

    if (not state->bActivateButton) { return; }

    // Expose add session button only when conditions are valid
    ImGui::Spacing();

    if ((ImGui::Button("Create", {-1, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
    {
        RegisterSessionMainThread(state->UriBuffer, state->Selected);
        ImGui::MarkIniSettingsDirty();
        state->bActivateButton      = false;
        state->bSetNextFocusToInput = true;
    }
}

shared_ptr<ISession> CreatePerfkitTcpRawClient();

bool                 Application::RegisterSessionMainThread(
                        string keyString, ESessionType type, string_view optionalDefaultDisplayName)
{
    if (isSessionExist(keyString, type))
    {
        NotifyToast{"Session Creation Failed"}
                .Error()
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
        NotifyToast{"Session Creation Failed"}
                .Error()
                .String("URI [{}]: Given session type is not implemented yet ...", keyString);

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

    NotifyToast{"Session Created"}.String("{}@{}", elem->CachedDisplayName, elem->Key);

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

void Application::tickSessions()
{
    for (auto& sess : _sessions)
    {
        sess.Ref->TickSession();
        if (not sess.bShow) { continue; }

        CPPH_CALL_ON_EXIT(ImGui::End());
        auto nameStr = usprintf("%s@%s###%s-%d.SSNWND",
                                sess.CachedDisplayName.c_str(),
                                sess.Key.c_str(),
                                sess.Key.c_str(),
                                sess.Type);

        if (not ImGui::Begin(nameStr, &sess.bShow)) { continue; }
        sess.Ref->RenderTickSession();
    }
}
