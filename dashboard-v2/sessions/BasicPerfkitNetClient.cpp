//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <cpph/app/localize.hpp>
#include <cpph/helper/macros.hxx>
#include <cpph/refl/object.hxx>
#include <cpph/refl/rpc/rpc.hxx>
#include <cpph/refl/rpc/service.hxx>
#include <perfkit/configs.h>

#include "Application.hpp"
#include "imgui_extension.h"
#include "utils/Misc.hpp"

using namespace perfkit;
using namespace net::message;

class PerfkitNetClientRpcMonitor : public rpc::if_session_monitor
{
   public:
    std::weak_ptr<BasicPerfkitNetClient> _owner;

    void on_session_expired(rpc::session_profile_view profile) noexcept override
    {
        if (auto lc = _owner.lock())
            lc->_onSessionDispose_(profile);
    }
};

BasicPerfkitNetClient::BasicPerfkitNetClient()
{
    // Create service
    auto service_info = rpc::service_builder{};
    service_info
            .route(notify::tty,
                   [this](tty_output_t& h) { _ttyQueue.lock()->append(h.content); })
            .route(notify::update_config_category,
                   bind_front(&decltype(_wndConfig)::HandleNewConfigClass, &_wndConfig))
            .route(notify::deleted_config_category,
                   bind_front(&decltype(_wndConfig)::HandleDeletedConfigClass, &_wndConfig))
            .route(notify::config_entity_update,
                   bind_front(&decltype(_wndConfig)::HandleConfigUpdate, &_wndConfig))
            .route(notify::session_status,
                   [this](notify::session_status_t const& arg) {
                       PostEventMainThreadWeak(weak_from_this(), [this, arg] { _sessionStats = arg; });
                   });

    _wndTrace.BuildService(service_info);
    _wndGraphics.BuildService(service_info);

    _notify_handler = service_info.build();

    // Create monitor
    auto monitor = std::make_shared<PerfkitNetClientRpcMonitor>();
    _monitor = monitor;

    // Tty config
    _tty.SetReadOnly(true);
}

void BasicPerfkitNetClient::InitializeSession(const string& keyUri)
{
    // Config Load/Store
    gApp->OnLoadWorkspace.add_weak(
            weak_from_this(),
            [this] {
                _uiState.bTraceOpen = RefPersistentNumber("%s.WndTrace", _key.c_str());
                _uiState.bConfigOpen = RefPersistentNumber("%s.WndConfig", _key.c_str());
                _uiState.bGraphicsOpen = RefPersistentNumber("%s.WndGraphics", _key.c_str());
            });

    gApp->OnDumpWorkspace.add_weak(
            weak_from_this(),
            [this] {
                RefPersistentNumber("%s.WndTrace", _key.c_str()) = _uiState.bTraceOpen;
                RefPersistentNumber("%s.WndConfig", _key.c_str()) = _uiState.bConfigOpen;
                RefPersistentNumber("%s.WndGraphics", _key.c_str()) = _uiState.bGraphicsOpen;
            });

    _displayKey = _key = keyUri;
    ((PerfkitNetClientRpcMonitor*)&*_monitor)->_owner = weak_from_this();
}

void BasicPerfkitNetClient::FetchSessionDisplayName(std::string* outName)
{
    if (not IsSessionOpen())
        return;

    outName->clear();
    fmt::format_to(
            std::back_inserter(*outName), "{}@{}",
            _sessionInfo.name, _sessionInfo.hostname);
}

void BasicPerfkitNetClient::RenderTickSession()
{
    // State summary (bandwidth, memory usage, etc ...)
    ImGui::PushStyleColor(ImGuiCol_Text, IsSessionOpen() ? 0xff'00ff00 : ImGui::GetColorU32(ImGuiCol_TextDisabled));
    bool bKeepConnection = true;
    auto bOpenSessionInfoHeader = ImGui::CollapsingHeader(
            usprintf("%s##SessInfo", _key.c_str()),
            IsSessionOpen() ? &bKeepConnection : nullptr,
            ImGuiTreeNodeFlags_AllowItemOverlap);
    ImGui::PopStyleColor(1);

    // Draw subwidget checkboxes
    {
        auto fnWrapCheckbox =
                [&](const char* label, bool* ptr) {
                    CPPH_FINALLY(ImGui::PopStyleColor(2));
                    if (*ptr) {
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    } else {
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                    }

                    ImGui::SameLine();
                    if (ImGui::SmallButton(label)) { *ptr = !*ptr; }
                };

        ImGui::SameLine(0, 10), ImGui::Text("");
        fnWrapCheckbox(LOCWORD(" configs "), &_uiState.bConfigOpen);
        fnWrapCheckbox(LOCWORD(" traces "), &_uiState.bTraceOpen);
        fnWrapCheckbox(LOCWORD(" graphics "), &_uiState.bGraphicsOpen);
    }

    if (bOpenSessionInfoHeader)
        if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"SummaryGroup"}}) {
            auto bRenderEntityContent = ShouldRenderSessionListEntityContent();
            auto width = ImGui::GetContentRegionAvail().x * 3 / 5 * (bRenderEntityContent);
            if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"SessionState", width, false}}) {
                ImGui::BulletText("Stats");
                ImGui::Separator();

                drawSessionStateBox();
            }

            if (bRenderEntityContent) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, 0xff121212);
                if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"ConnectionInfo"}}) {
                    ImGui::PopStyleColor();
                    RenderSessionListEntityContent();
                } else {
                    ImGui::PopStyleColor();
                }
            }
        }

    if (not bKeepConnection) {
        CloseSession();
    }

    auto fnRenderTTY
            = [&] {
                  auto bDrawBorder = CondInvokeBody(
                          IsSessionOpen() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows),
                          &ImGui::PopStyleColor, 1);

                  if (bDrawBorder) {
                      ImGui::PushStyleColor(ImGuiCol_Border, 0xff'117712);
                  }

                  if (CPPH_FINALLY(ImGui::EndChild()); ImGui::BeginChild("TerminalGroup", {}, true))
                      drawTTY();
              };

    auto fnRenderTrace = [&] { _wndTrace.Render(); };
    auto fnRenderGraphics = [&] { _wndGraphics.Render(); };

    auto idTTY = ImGui::GetID("TTY");
    auto idTrace = ImGui::GetID("Trace");
    auto idGraphics = ImGui::GetID("Graphics");

    float* TTYSpanWidth = &_uiState.TTYSpanWidth;
    float tmpZeroSpanBody = 0;
    if (not _uiState.bTraceOpen && not _uiState.bGraphicsOpen) { TTYSpanWidth = &tmpZeroSpanBody; }

    ImGui::SplitRenders(
            TTYSpanWidth, 0,
            idTTY, ImGui::GetID("TTY_SIDE"),
            fnRenderTTY, [&] {
                if (_uiState.bTraceOpen && _uiState.bGraphicsOpen)
                    ImGui::SplitRenders(&_uiState.TraceSpanWidth, 0, idTrace, idGraphics, fnRenderTrace, fnRenderGraphics);
                else if (_uiState.bTraceOpen)
                    ImGui::BeginChild(idTrace), fnRenderTrace(), ImGui::EndChild();
                else if (_uiState.bGraphicsOpen)
                    ImGui::BeginChild(idGraphics), fnRenderGraphics(), ImGui::EndChild();
            },
            ImGuiSplitRenderFlags_Vertical);
}

void BasicPerfkitNetClient::TickSession()
{
    bool const bIsSessionOpenCache = IsSessionOpen();
    if (bIsSessionOpenCache) { tickHeartbeat(); }

    _wndConfig.Tick();
    _wndTrace.Tick();
    _wndGraphics.Tick(&_uiState.bGraphicsOpen);

    if (_uiState.bConfigOpen) {
        ImGui::SetNextWindowSize({240, 320}, ImGuiCond_Once);
        if (CPPH_FINALLY(ImGui::End()); ImGui::Begin(LOCWORD("configs"), nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            _wndConfig.Render(&_uiState.bConfigOpen);
        }
    }
}

void BasicPerfkitNetClient::_onSessionCreate_(rpc::session_profile_view profile)
{
    auto anchor = make_shared<nullptr_t>();
    NotifyToast(LOCTEXT("Rpc Session Created")).String(profile->peer_name);

    try {
        auto rpc = profile->w_self.lock();

        auto sesionInfo = decltype(service::session_info)::return_type{};
        service::session_info(rpc).request_with(&sesionInfo, 1s);

        auto ttyContent = decltype(service::fetch_tty)::return_type{};
        service::fetch_tty(rpc).request_with(&ttyContent, 0);

        PostEventMainThreadWeak(
                weak_from_this(),
                [this,
                 rpc = move(rpc),
                 info = std::move(sesionInfo),
                 peer = profile->peer_name,
                 ttyContent = std::move(ttyContent),
                 anchor = anchor]() mutable {
                    _rpc.reset();
                    _rpc = move(rpc);

                    _sessionAnchor = anchor;
                    _sessionInfo = std::move(info);
                    _displayKey = fmt::format(
                            "{}@{} [{}]", _sessionInfo.name, _sessionInfo.hostname, _key);

                    auto introStr = fmt::format(
                            "\n\n"
                            "+---------------------------------------- NEW SESSION -----------------------------------------\n"
                            "| \n"
                            "| \n"
                            "| Name               : {}\n"
                            "| Host               : {}\n"
                            "| Peer               : {}\n"
                            "| Number of cores    : {}\n"
                            "| \n"
                            "| {}\n"
                            "+----------------------------------------------------------------------------------------------\n"
                            "\n\n",
                            _sessionInfo.name,
                            _sessionInfo.hostname,
                            peer,
                            _sessionInfo.num_cores,
                            _sessionInfo.description);

                    _ttyQueue.access(
                            [&](string& str) {
                                str.append(introStr);
                                str.append(ttyContent.content);
                            });
                });
    } catch (rpc::request_exception& ec) {
        NotifyToast{"Rpc invocation failed"}.Error().String(ec.what());
        return;
    }
}

void BasicPerfkitNetClient::_onSessionDispose_(rpc::session_profile_view profile)
{
    NotifyToast("Rpc Session Disposed").Warning().String(profile->peer_name);
    _ttyQueue.access([&](auto&& str) {
        str.append(fmt::format(
                std::locale("en_US.utf-8"),
                "\n\n"
                "<eof>\n"
                "    [PEER] {}\n"
                "\n"
                "    [Rx] {:<24L} bytes\n"
                "    [Tx] {:<24L} bytes\n"
                "</eof>\n"
                "\n\n",
                profile->peer_name,
                profile->total_read,
                profile->total_write));
    });

    PostEventMainThread(bind_front_weak(weak_from_this(), [this] { CloseSession(); }));
}

BasicPerfkitNetClient::~BasicPerfkitNetClient()
{
    std::weak_ptr anchor = std::exchange(_rpcFlushGuard, {});
    while (not anchor.expired()) { std::this_thread::sleep_for(10ms); }

    _rpc.reset();
}

void BasicPerfkitNetClient::tickHeartbeat()
{
    if (not _rpc) { return; }
    if (not _timHeartbeat.check_sparse()) { return; }

    if (_hrpcHeartbeat && not _hrpcHeartbeat.wait(0ms)) {
        NotifyToast{LOCTEXT("Heartbeat failed")}.Error();

        if (++_heartbeatFailCount == 5) {
            CloseSession();
        }

        return;
    }

    try {
        _hrpcHeartbeat = service::heartbeat(_rpc).async_request(
                [](auto&& ec, auto content) {
                    if (ec)
                        NotifyToast{LOCTEXT("Heartbeat returned error")}
                                .Error()
                                .String(content);
                });
    } catch (...) {
    }
}

void BasicPerfkitNetClient::CloseSession()
{
    _sessionAnchor.reset();
    _heartbeatFailCount = 0;

    if (_hrpcHeartbeat) { _hrpcHeartbeat.abort(); }
    if (_hrpcLogin) { _hrpcLogin.abort(); }
    _authLevel = message::auth_level_t::unauthorized;
    _sessionStats = {};

    _wndConfig.ClearContexts();
    _rpc.reset();
}

void BasicPerfkitNetClient::drawTTY()
{
    struct TtyContext {
        poll_timer timColorize{250ms};
        bool bScrollLock = false;
        int colorizeFence = 0;

        char cmdBuf[512];
        int cmdHistoryCursor = 0;

        float uiControlPadHeight = 0;

        //
        circular_queue<string> cmdHistory{256};
    };
    auto& _ = RefAny<TtyContext>("TTY");
    bool bFrameHasInput = false;

    // Retrieve buffer content
    _ttyQueue.access([&](string& str) {
        if (str.empty()) { return; }
        xterm_leap_escape(&_tty, str);
        str.clear();

        bFrameHasInput = true;
    });

    // When line exceeds maximum allowance ...
    if (auto ntot = _tty.GetTotalLines(); ntot > 17999) {
        auto lines = _tty.GetTextLines();
        lines.erase(lines.begin(), lines.begin() + 7999);

        _tty.SetTextLines(lines);
        _tty.ForceColorize(_tty.GetTotalLines() - 128, -128);
        _.colorizeFence = _tty.GetTotalLines();
    }

    // Apply colorization
    // Limited number of lines can be colorized at once
    if (_.timColorize.check_sparse())
        if (auto ntot = _tty.GetTotalLines(); ntot != _.colorizeFence) {
            _.colorizeFence = std::max(_.colorizeFence, ntot - 128);
            _tty.ForceColorize(_.colorizeFence - 1, -128);
            _.colorizeFence = _tty.GetTotalLines();
        }

    // Scroll Lock
    if (bFrameHasInput && not _.bScrollLock) {
        _tty.MoveBottom();
        _tty.MoveEnd();
        auto xscrl = ImGui::GetScrollX();
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        ImGui::SetScrollX(xscrl);
    }

    // Render
    _tty.Render("Terminal", {0, -_.uiControlPadHeight}, true);

    ImGui::Spacing();

    {
        auto beginCursorPos = ImGui::GetCursorPosY();

        if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"ConfPanel"}}) {
            ImGui::Checkbox(LOCWORD("Scroll Lock"), &_.bScrollLock);
            ImGui::SameLine();

            if (ImGui::Button(LOCWORD(" clear "))) {
                _.colorizeFence = 0;
                _tty.SetReadOnly(false);
                _tty.SelectAll();
                _tty.Delete();
                _tty.SetReadOnly(true);
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::SameLine();

            auto const inputFlags
                    = ImGuiInputTextFlags_EnterReturnsTrue
                    | ImGuiInputTextFlags_CallbackHistory;

            auto fnTextCallback =
                    [](ImGuiInputTextCallbackData* cbData) {
                        auto& _ = *(TtyContext*)cbData->UserData;
                        int historyCursorDelta = 0;

                        switch (cbData->EventFlag) {
                            case ImGuiInputTextFlags_CallbackHistory: {
                                historyCursorDelta += (cbData->EventKey == ImGuiKey_UpArrow);
                                historyCursorDelta -= (cbData->EventKey == ImGuiKey_DownArrow);

                                if (historyCursorDelta != 0) {
                                    _.cmdHistoryCursor = std::clamp<int>(
                                            _.cmdHistoryCursor + historyCursorDelta,
                                            0, _.cmdHistory.size());

                                    if (auto iter = _.cmdHistory.end() - _.cmdHistoryCursor; iter != _.cmdHistory.end()) {
                                        cbData->DeleteChars(0, cbData->BufTextLen);
                                        cbData->InsertChars(0, iter->c_str());
                                    }
                                }

                                break;
                            }

                            default:
                                break;
                        }

                        return 0;
                    };

            auto bEnterPressed = ImGui::InputTextWithHint(
                    "##EnterCommand",
                    LOCTEXT("Enter Command Here"),
                    _.cmdBuf,
                    sizeof _.cmdBuf,
                    inputFlags, fnTextCallback, &_);

            if (bEnterPressed) {
                ImGui::SetKeyboardFocusHere(-1);
            }
            if (_rpc && bEnterPressed) {
                string cmd = _.cmdBuf;
                _.cmdBuf[0] = 0;  // Clear command buffer.

                // Send command invocation request
                message::service::invoke_command(_rpc).notify(cmd);

                // Push command content to history queue
                if (not cmd.empty() && (_.cmdHistory.empty() || _.cmdHistory.back() != cmd)) {
                    _.cmdHistory.emplace_back(std::move(cmd));
                }
                _.cmdHistoryCursor = 0;
            }
        }

        _.uiControlPadHeight = ImGui::GetCursorPosY() - beginCursorPos;
    }
}

bool BasicPerfkitNetClient::ShouldRenderSessionListEntityContent() const
{
    return true;
}

void BasicPerfkitNetClient::RenderSessionListEntityContent()
{
    if (not IsSessionOpen()) {
        RenderSessionOpenPrompt();
    } else if (_authLevel == message::auth_level_t::unauthorized) {
        if (not _hrpcLogin) {
            // Draw login prompt
            static char buf[256];

            ImGui::Text("ID"), ImGui::SameLine(30), ImGui::SetNextItemWidth(-1);
            ImGui::InputText(usprintf("##ID.%p", this), buf, sizeof buf);
            ImGui::Text("PW"), ImGui::SameLine(30), ImGui::SetNextItemWidth(-1);
            ImGui::InputText(usprintf("##PW.%p", this), buf, sizeof buf);

            ImGui::Spacing();
            if (ImGui::Button(usprintf(LOCWORD("LOGIN##%p"), this), {-1, 0})) {
                auto fnOnLogin
                        = [this] {
                              service::request_republish_registries(_rpc).notify();
                          };

                auto fnOnRpcComplete
                        = [this, fnOnLogin](auto&& ec, auto content) {
                              if (ec) {
                                  NotifyToast{LOCTEXT("[{}]\nLogin Failed"), _key}
                                          .String(ec.message())
                                          .Error();

                                  PostEventMainThreadWeak(
                                          weak_from_this(), [=] { _hrpcLogin.reset(); });
                              } else {
                                  NotifyToast{LOCTEXT("[{}]\nLogin Successful"), _key}
                                          .String(LOCTEXT("You have {} access"), _authLevel == message::auth_level_t::admin_access ? LOCWORD("admin") : LOCWORD("basic"));
                                  PostEventMainThreadWeak(
                                          weak_from_this(), [=] { _hrpcLogin.reset(), fnOnLogin(); });
                              }
                          };

                if (not _rpc) {
                    CloseSession();
                    return;
                }

                _hrpcLogin = service::login(_rpc).async_request(
                        &_authLevel,
                        "serialized_content",
                        bind_front_weak(_sessionAnchor, fnOnRpcComplete));

                NotifyToast{LOCWORD("[{}]\nLogging in ..."), _key}
                        .Spinner()
                        .Permanent()
                        .Custom([this] { return _hrpcLogin; })
                        .OnForceClose([this] { _hrpcLogin.abort(); });
            }
        } else {
            // Draw logging in ... content
            ImGui::Text(LOCWORD("Logging in ..."));
        }
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::Text(LOCWORD("Config")), ImGui::SameLine();
        ImGui::ToggleButton("ToggleConfig", &_uiState.bConfigOpen);

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(LOCWORD("Trace")), ImGui::SameLine();
        ImGui::ToggleButton("ToggleTrace", &_uiState.bTraceOpen);

        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::Text(LOCWORD("Graphic")), ImGui::SameLine();
        ImGui::ToggleButton("ToggleGraphics", &_uiState.bGraphicsOpen);
    }
}

void BasicPerfkitNetClient::drawSessionStateBox()
{
    if (_authLevel < net::message::auth_level_t::basic_access) {
        ImGui::TextDisabled("-- DISABLED --");
        return;
    }

    auto& stat = _sessionStats;
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, 0xff777777);

    ImGui::TextDisabled("Rx:"), ImGui::SameLine();
    ImGui::TextUnformatted(FormatBitText(stat.bw_in, true, true));
    ImGui::SameLine(), ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x / 2);
    ImGui::TextDisabled("Tx:"), ImGui::SameLine();
    ImGui::TextUnformatted(FormatBitText(stat.bw_out, true, true));

    ImGui::TextDisabled("MEM[VIRT]:"), ImGui::SameLine();
    ImGui::TextUnformatted(FormatBitText(stat.memory_usage_virtual, false, false));
    ImGui::SameLine(), ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x / 2);
    ImGui::TextDisabled("MEM[RES]:"), ImGui::SameLine();
    ImGui::TextUnformatted(FormatBitText(stat.memory_usage_resident, false, false));

    ImGui::TextDisabled("THRD:"), ImGui::SameLine();
    ImGui::Text("%d", stat.num_threads);
    ImGui::SameLine(0, 0), ImGui::TextDisabled(" threads active");

    ImGui::TextDisabled("CPU[SYS]:"), ImGui::SameLine();
    ImGui::Text("%.2f", (stat.cpu_usage_total_system + stat.cpu_usage_total_user) * 100.);
    ImGui::SameLine(0, 0), ImGui::TextDisabled(" / 100%%");
    ImGui::SameLine();
    ImGui::TextDisabled("CPU[SELF]:"), ImGui::SameLine();
    ImGui::Text("%.2f", (stat.cpu_usage_self_system + stat.cpu_usage_self_user) * 100.);
    ImGui::SameLine(0, 0), ImGui::TextDisabled(" / %.0f%%", 100. * _sessionInfo.num_cores);

    ImGui::PopStyleColor();
}
