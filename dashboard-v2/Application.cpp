//
// Created by ki608 on 2022-03-14.
//

#include "Application.hpp"

#include <cassert>
#include <memory>
#include <thread>

#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <cpph/algorithm/std.hxx>
#include <cpph/helper/macros.hxx>
#include <cpph/utility/cleanup.hxx>
#include <imgui.h>
#include <imgui_extension.h>
#include <imgui_internal.h>
#include <perfkit/configs.h>
#include <perfkit/localize.h>
#include <spdlog/spdlog.h>

#include "interfaces/Session.hpp"
#include "stdafx.h"
#include "widgets/TimePlot.hpp"

#define LT PERFKIT_C_LOCTEXT
#define LW PERFKIT_C_LOCWORD
#define KW PERFKIT_C_KEYWORD
#define KT PERFKIT_C_KEYTEXT

static auto PersistentNumberStorage()
{
    static std::map<string, double, std::less<>> _storage;
    return &_storage;
}

void PostAsyncEvent(ufunction<void()> evt)
{
    asio::post(std::move(evt));
}

PERFKIT_CATEGORY(GConfig)
{
    PERFKIT_CONFIGURE(Locale, "en-US");

    PERFKIT_SUBCATEGORY(Application)
    {
        struct SessionArchive {
            string key;
            int type = 0;
            string displayName;
            bool bShow = false;

            CPPH_REFL_DEFINE_OBJECT_inline_simple(key, type, displayName, bShow);
        };

        PERFKIT_CONFIGURE(ArchivedSessions, vector<SessionArchive>{});
        PERFKIT_CONFIGURE(Numbers, (std::map<string, double, std::less<>>{}));
    }

    PERFKIT_SUBCATEGORY(Widgets) {}
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
    if (_bShowStyles)
        if (CPPH_FINALLY(ImGui::End()); ImGui::Begin(LOCTEXT("Styles"), &_bShowStyles)) { ImGui::ShowStyleEditor(nullptr); }
    if (_bShowDemo) { ImGui::ShowDemoWindow(&_bShowDemo); }

    tickSessions();

    // Tick graphics
    tickGraphicsMainThread();

    // Tick time plots
    _timePlot->TickWindow();
}

void Application::PostMainThreadEvent(perfkit::ufunction<void()> callable)
{
    asio::post(*_ioc, std::move(callable));
}

void Application::DispatchMainThreadEvent(perfkit::ufunction<void()> callable)
{
    asio::dispatch(*_ioc, std::move(callable));
}

void VerifyMainThread()
{
    assert((default_singleton<std::thread::id, class Application>()) == std::this_thread::get_id());
}

TimePlotSlotProxy CreateTimePlot(string name)
{
    return Application::Get()->TimePlotManager()->CreateSlot(std::move(name));
}

Application::Application()
        : _ioc(std::make_unique<asio::io_context>())
{
    default_singleton<std::thread::id, class Application>() = std::this_thread::get_id();

    //
    // Register INI handler to restore previous workspace configuration file
    //
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
                  if (strncmp(data, "WorkspaceFile", strlen("WorkspaceFile")) == 0) {
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

    //
    // Register workspace reload event for sessions
    //

    OnLoadWorkspace.add(
            [this] {
                GConfig::update();

                // Load localization
                auto localePath = "rsrc/locale/" + *GConfig::Locale + ".json";
                perfkit::load_localization_lut(*GConfig::Locale, localePath);
                perfkit::select_locale(*GConfig::Locale);

                // Load Sessions
                _sessions.clear();

                //
                {
                    auto CreateSessionDiscoverAgent()->shared_ptr<ISession>;
                    auto elem = &_sessions.emplace_back();
                    elem->bTransient = true;
                    elem->Key = "Discovered Sessions";
                    elem->Type = ESessionType::None;
                    elem->Ref = CreateSessionDiscoverAgent();

                    elem->Ref->InitializeSession({});
                }

                for (auto& desc : *GConfig::Application::ArchivedSessions) {
                    auto sess = RegisterSessionMainThread(desc.key, ESessionType(desc.type), desc.displayName);
                    sess->bShow = desc.bShow;
                }

                // Load global variables
                *PersistentNumberStorage() = *GConfig::Application::Numbers;

                NotifyToast{KT(APP_CONFIG_LOADED, "App Config Loaded")}
                        .Trivial()
                        .String(LT("{} sessions, {} numbers loaded"),
                                _sessions.size(), PersistentNumberStorage()->size());
            },
            perfkit::event_priority::first);

    // Export Sessions
    OnDumpWorkspace.add(
            [this] {
                std::vector<GConfig::Application::SessionArchive> archive;
                archive.reserve(_sessions.size());

                for (auto& sess : _sessions) {
                    if (sess.bTransient)
                        continue;

                    auto arch = &archive.emplace_back();
                    arch->key = sess.Key;
                    arch->displayName = sess.CachedDisplayName;
                    arch->type = int(sess.Type);
                    arch->bShow = sess.bShow;
                }

                GConfig::Application::ArchivedSessions.commit(std::move(archive));
                GConfig::Application::Numbers.commit(*PersistentNumberStorage());

                perfkit::dump_localization_lut("rsrc/locale/" + *GConfig::Locale + ".json");
            },
            perfkit::event_priority::last);
}

void Application::Initialize()
{
    // Initialize after application is ready ..
    _timePlot = make_unique<TimePlotWindowManager>();
}

Application::~Application()
{
    list<weak_ptr<ISession>> sessions;
    transform(_sessions, back_inserter(sessions), [](decltype(_sessions[0]) s) { return s.Ref; });
    _sessions.clear();

    SPDLOG_INFO("Waiting all sessions correctly erased ...");
    for (auto& ws : sessions) {
        while (not ws.expired()) {
            std::this_thread::sleep_for(10ms);
        }
    }
    SPDLOG_INFO("Done.");
}

void Application::drawMenuContents()
{
    if (not ImGui::BeginMainMenuBar()) { return; }
    CPPH_FINALLY(ImGui::EndMainMenuBar());

    if (CondInvoke(ImGui::BeginMenu(LW("File")), &ImGui::EndMenu)) {
        if (ImGui::MenuItem(LW("Save workspace")))
            saveWorkspace();
        if (ImGui::MenuItem(LW("Save workspace as")))
            NotifyToast{}.Fatal().String("Not Implemented: " __FILE__ ":{}", __LINE__);
        if (ImGui::MenuItem(LW("Load workspace")))
            NotifyToast{}.Fatal().String("Not Implemented: " __FILE__ ":{}", __LINE__);
    }

    if (CondInvoke(ImGui::BeginMenu(LW("View")), &ImGui::EndMenu)) {
        ImGui::MenuItem(LOCTEXT("Sessions"), "Ctrl+H", &_bDrawSessionList);

        ImGui::Separator();
        ImGui::MenuItem(LW("Metrics"), NULL, &_bShowMetrics);
        ImGui::MenuItem(LW("Styles"), NULL, &_bShowStyles);
        ImGui::Separator();
        ImGui::MenuItem(LW("Demo"), NULL, &_bShowDemo);
    }

    if (CondInvoke(ImGui::BeginMenu(LW("Configure")), &ImGui::EndMenu)) {
        if (CondInvoke(ImGui::BeginMenu(LW("Add Session...")), &ImGui::EndMenu))
            drawAddSessionMenu();

        if (CondInvoke(ImGui::BeginMenu(LW("Scale")), &ImGui::EndMenu)) {
            // TODO: DPI Update application
            static auto pivotStyleVars = ImGui::GetStyle();
            static auto previousDpi = 0;
            bool bDpiUpdate = false;

            if (previousDpi != ImGui::GetWindowDpiScale()) {
                previousDpi = exchange(ImGui::GetIO().FontGlobalScale, ImGui::GetWindowDpiScale());
                bDpiUpdate = true;
            }

            auto startCursorPos = ImGui::GetCursorPos();
            int scaleFactor = roundl(ImGui::GetIO().FontGlobalScale * 4.f);
            ImGui::SetNextItemWidth(250 * DpiScale());
            ImGui::ProgressBar((scaleFactor - 3) / 7.f, {-FLT_MIN, 0}, "");

            ImGui::SetCursorPos(startCursorPos);
            ImGui::SetNextItemWidth(250 * DpiScale());

            ImGui::PushStyleColor(ImGuiCol_FrameBg, 0);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, 0);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, 0);
            if (bDpiUpdate || ImGui::DragInt("##Scale", &scaleFactor, 0.005, 4, 10)) {
                auto fScaleFactor = scaleFactor / 4.f;
                auto style = pivotStyleVars;

                ImGui::GetIO().FontGlobalScale = fScaleFactor;
                style.ScaleAllSizes(fScaleFactor);
                ImGui::GetStyle() = style;
            }
            ImGui::PopStyleColor(3);
        }
    }
}

void Application::drawSessionList(bool* bKeepOpen)
{
    CPPH_FINALLY(ImGui::End());
    if (not ImGui::Begin(LW("Sessions"), bKeepOpen)) { return; }

    if (ImGui::TreeNodeEx(LW("Add Session"), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        if (CPPH_TMPVAR = ImGui::ScopedChildWindow("Session-AddNew"))
            drawAddSessionMenu();

    ImGui::AlignTextToFramePadding(), ImGui::BulletText(LW("Sessions"));
    CPPH_FINALLY(ImGui::EndChild());
    ImGui::BeginChild("Session-List", {0, 0}, true);

    char textBuf[256];
    auto dragDropSwap{optional<pair<int64_t, int64_t>>{}};

    for (auto iter = _sessions.begin(); iter != _sessions.end();) {
        auto& sess = *iter;
        sess.Ref->FetchSessionDisplayName(&sess.CachedDisplayName);

        bool const bIsSessionOpen = sess.Ref->IsSessionOpen();
        bool bOpenStatus = true;
        auto headerFlag = 0;
        CPPH_FINALLY(ImGui::PopStatefulColors());

        auto baseColor = bIsSessionOpen ? 0xff'113d16 : 0xff'080808;
        baseColor = sess.bPendingClose ? 0xff'37b8db : baseColor;
        sess.bPendingClose = sess.bPendingClose && bIsSessionOpen;
        bool bRenderContents = sess.Ref->ShouldRenderSessionListEntityContent();

        if (not bRenderContents) {
            headerFlag |= ImGuiTreeNodeFlags_Bullet;
            ImGui::PushStatefulColorsUni(ImGuiCol_Header, baseColor);
        } else {
            ImGui::PushStatefulColors(ImGuiCol_Header, baseColor);
        }

        sprintf(textBuf, "%s###SLB-%s-%d",
                sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);

        ImGui::PushStyleColor(ImGuiCol_Header, sess.bTransient ? 0xff311424 : ImGui::GetColorU32(ImGuiCol_Header));
        ImGui::PushStyleColor(ImGuiCol_Text, bIsSessionOpen ? 0xffffffff : 0xffbbbbbb);
        bool* openBtnPtr = bIsSessionOpen || sess.Ref->CanDeleteSession() ? &bOpenStatus : nullptr;
        bRenderContents &= ImGui::CollapsingHeader(textBuf, openBtnPtr, headerFlag);
        ImGui::PopStyleColor(2);

        if (sess.Ref->CanOpenSession() and ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            sess.bShow = not sess.bShow;
        }

        if (CondInvoke(ImGui::BeginDragDropSource(ImGuiDragDropFlags_None), ImGui::EndDragDropSource)) {
            auto index = iter - _sessions.begin();
            ImGui::SetDragDropPayload("DND_PAYLOAD_SESSION_LIST_SWAP", &index, sizeof index);
            ImGui::Text("%s [%s]", sess.CachedDisplayName.c_str(), sess.Key.c_str());
        }

        if (CondInvoke(ImGui::BeginDragDropTarget(), ImGui::EndDragDropTarget)) {
            if (auto payload = ImGui::AcceptDragDropPayload("DND_PAYLOAD_SESSION_LIST_SWAP")) {
                auto index = iter - _sessions.begin();
                auto source = *(decltype(index)*)payload->Data;

                if (index != source) { dragDropSwap = make_pair(source, index); }
            }
        }

        if (bIsSessionOpen) {
            ImGui::SameLine();
            ImGui::TextColored({0, 1, 0, 1}, " [%c]", "-\\|/"[int(ImGui::GetTime() / 0.33) % 4]);
        }

        if (sess.bShow) {
            ImGui::SameLine();
            ImGui::TextColored({1, 1, 0, 1}, "[*]");
        }

        ImGui::SameLine();
        ImGui::TextColored({.5f, .5f, .5f, 1.f}, "%s", sess.Key.c_str());

        if (bRenderContents) {
            sprintf(textBuf, "%s##CHLD-%s-%d", sess.CachedDisplayName.c_str(), sess.Key.c_str(), sess.Type);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg) - ImVec4{.1, .1, .1, .0});
            CPPH_FINALLY(ImGui::PopStyleColor());

            if (CPPH_TMPVAR = ImGui::ScopedChildWindow(textBuf)) {
                sess.Ref->RenderSessionListEntityContent();
            }
        }

        sprintf(textBuf, LW("Unregister##%s-%d"), sess.Key.c_str(), sess.Type);
        if (not bOpenStatus) {
            if (bIsSessionOpen && not sess.bPendingClose) {
                // If session was originally open, try close session.
                sess.Ref->CloseSession();
                sess.bPendingClose = true;

                NotifyToast(LW("Session Closed")).String("{}@{}", sess.CachedDisplayName, sess.Key);
            } else {
                // Otherwise, popup modal for deleting this session
                ImGui::OpenPopup(textBuf);
            }
        }

        if (CondInvoke(ImGui::BeginPopup(textBuf), ImGui::EndPopup)) {
            sess.bPendingClose = false;

            ImGui::TextUnformatted(LW("Are you sure to unregister this session?"));
            if (ImGui::Button(KW(Yes))) {
                auto name = iter->Key;

                iter = _sessions.erase(iter);
                ImGui::CloseCurrentPopup();
                ImGui::MarkIniSettingsDirty();

                NotifyToast(LW("Session Erased")).String(name);
                continue;
            }

            ImGui::SameLine();
            if (ImGui::Button(KW(No))) {
                ImGui::CloseCurrentPopup();
            }
        }

        ++iter;
    }

    if (dragDropSwap && (dragDropSwap->first < _sessions.size() && dragDropSwap->second < _sessions.size())) {
        auto [from, to] = *dragDropSwap;
        auto temp = move(_sessions[from]);
        _sessions.erase(_sessions.begin() + from);
        _sessions.insert(_sessions.begin() + to, move(temp));
    }
}

void Application::drawAddSessionMenu()
{
    char const* ItemNames[] = {"-- NONE --", "Tcp [unsafe]", "Tcp [ssl]", LW("Relay Server"), LW("websocket")};
    auto state = &_addSessionModalState;

    static_assert(std::size(ItemNames) == int(ESessionType::ENUM_MAX_VALUE));

    if (CondInvoke(ImGui::BeginCombo(LW("Session Type"), ItemNames[int(state->Selected)]), &ImGui::EndCombo)) {
        for (int i = 1; i < int(ESessionType::ENUM_MAX_VALUE); ++i) {
            bool bIsSelected = (i == int(state->Selected));
            if (ImGui::Selectable(ItemNames[i], bIsSelected)) {
                state->Selected = ESessionType(i);
                state->bActivateButton = 0 < strlen(state->UriBuffer);
                state->bSetNextFocusToInput = true;
            }

            if (bIsSelected)
                ImGui::SetItemDefaultFocus();
        }
    }

    if (state->Selected == ESessionType::None) { return; }

    // Draw URI input box
    if (state->bSetNextFocusToInput) {
        state->bSetNextFocusToInput = false;
        ImGui::SetKeyboardFocusHere();
    }

    if (ImGui::InputText("URI", state->UriBuffer, sizeof state->UriBuffer, ImGuiInputTextFlags_AutoSelectAll)) {
        std::string_view uri = state->UriBuffer;

        if (uri.empty() || isSessionExist(uri, state->Selected)) {
            state->bActivateButton = false;
        } else {
            state->bActivateButton = true;
        }
    }

    if (not state->bActivateButton) { return; }

    // Expose add session button only when conditions are valid
    ImGui::Spacing();

    if ((ImGui::Button(LW("Create"), {-1, 0}) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        RegisterSessionMainThread(state->UriBuffer, state->Selected);
        ImGui::MarkIniSettingsDirty();
        state->bActivateButton = false;
        state->bSetNextFocusToInput = true;
    }
}

auto CreatePerfkitTcpRawClient() -> shared_ptr<ISession>;

auto Application::RegisterSessionMainThread(
        string keyString, ESessionType type, string_view optionalDefaultDisplayName, bool bTransient)
        -> SessionNode*
{
    if (isSessionExist(keyString, type)) {
        NotifyToast{LW("Session Creation Failed")}
                .Error()
                .String(LW("Session key {} already exist"), keyString);
        return nullptr;
    }

    shared_ptr<ISession> session;

    switch (type) {
        case ESessionType::TcpUnsafe:
            session = CreatePerfkitTcpRawClient();
            break;

        default:
            break;
    }

    if (not session) {
        NotifyToast{LW("Session Creation Failed")}
                .Error()
                .String(LT("URI [{}]: Given session type is not implemented yet ..."), keyString);

        return nullptr;
    }

    auto elem = &_sessions.emplace_back();
    elem->Key = std::move(keyString);
    elem->CachedDisplayName = optionalDefaultDisplayName;
    elem->Type = type;
    elem->Ref = std::move(session);
    elem->bShow = false;
    elem->bTransient = bTransient;

    elem->Ref->InitializeSession(elem->Key);
    elem->Ref->FetchSessionDisplayName(&elem->CachedDisplayName);

    NotifyToast{LW("Session Created")}.String("{}@{}", elem->CachedDisplayName, elem->Key);

    return elem;
}

bool Application::isSessionExist(std::string_view name, ESessionType type)
{
    using namespace perfkit::algorithm;

    auto predFindDup = [&](auto&& elem) { return elem.Key == name && elem.Type == type; };
    return (_sessions.end() != find_if(_sessions, predFindDup));
}

void Application::loadWorkspace()
{
    if (not perfkit::configs_import(_workspacePath)) {
        NotifyToast{}.Error().String(LW("Config path '{}' is not a valid file."), _workspacePath);
        return;
    }

    OnLoadWorkspace.invoke();
}

void Application::saveWorkspace()
{
    OnDumpWorkspace.invoke();

    perfkit::configs_export(_workspacePath);
}

void Application::tickSessions()
{
    for (auto& sess : _sessions) {
        sess.Ref->TickSession();
        if (not sess.bShow) { continue; }

        auto nameStr = usprintf("%s [%s]###%s.%d.SSNWND",
                                sess.CachedDisplayName.c_str(),
                                sess.Key.c_str(),
                                sess.Key.c_str(),
                                sess.Type);

        bool const bDrawGreenHeader = sess.Ref->IsSessionOpen();
        if (bDrawGreenHeader) {
            ImGui::PushStyleColor(ImGuiCol_TabActive, 0xff'257d47);
            ImGui::PushStyleColor(ImGuiCol_Tab, 0xff'124312);
            ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, 0xff'155d17);
            ImGui::PushStyleColor(ImGuiCol_TabUnfocused, 0xff'124312);
        }

        ImGui::SetNextWindowSize({640, 480}, ImGuiCond_Once);
        if (CPPH_FINALLY(ImGui::End()); ImGui::Begin(nameStr, &sess.bShow)) {
            sess.Ref->RenderTickSession();
        }

        ImGui::PopStyleColor(bDrawGreenHeader * 4);
    }

    if (auto wnd = ImGui::FindWindowByName(LW("configs")); wnd && ImGui::GetCurrentContext()->FrameCount == wnd->LastFrameActive) {
        if (CPPH_FINALLY(ImGui::End()); ImGui::Begin(LW("configs"))) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 600 * DpiScale());
        }
    }
}

//
Application* Application::Get()
{
    return &gApp.get();
}

double* detail::RefPersistentNumber(string_view name)
{
    auto& _storage = *PersistentNumberStorage();
    auto iter = _storage.find(name);

    if (iter == _storage.end())
        iter = _storage.try_emplace(std::string{name}, 0).first;

    return &iter->second;
}
