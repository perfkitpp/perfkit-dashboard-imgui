//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/refl/msgpack-rpc/context.hxx>
#include <perfkit/configs.h>

#include "imgui_extension.h"
#include "utils/Misc.hpp"

using namespace perfkit;
using namespace net::message;

class PerfkitNetClientRpcMonitor : public msgpack::rpc::if_context_monitor
{
   public:
    std::weak_ptr<BasicPerfkitNetClient> _owner;

   public:
    void on_new_session(const msgpack::rpc::session_profile& profile) noexcept override
    {
        if (auto lc = _owner.lock())
            lc->_onSessionCreate_(profile);
    }
    void on_dispose_session(const msgpack::rpc::session_profile& profile) noexcept override
    {
        if (auto lc = _owner.lock())
            lc->_onSessionDispose_(profile);
    }
};

BasicPerfkitNetClient::BasicPerfkitNetClient()
{
    // Create service
    auto service = msgpack::rpc::service_info{};
    service.route(notify::tty,
                  [this](tty_output_t& h) {
                      auto ref = _ttyQueue.lock();
                      ref->append(h.content);
                  });

    // Create monitor
    auto monitor = std::make_shared<PerfkitNetClientRpcMonitor>();
    _monitor     = monitor;

    // Create RPC context
    _rpc = std::make_unique<msgpack::rpc::context>(
            std::move(service),
            [guard = weak_ptr{_rpcFlushGuard}](auto&& fn) {
                asio::dispatch(
                        [guard = guard.lock(), fn = std::forward<decltype(fn)>(fn)] {
                            fn();
                        });
            },
            _monitor);

    // Tty config
    _tty.SetReadOnly(true);
}

void BasicPerfkitNetClient::InitializeSession(const string& keyUri)
{
    _key                                              = keyUri;
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
    ImGui::PushStyleColor(ImGuiCol_Header, IsSessionOpen() ? 0xff'257d47 : ImGui::GetColorU32(ImGuiCol_Header));
    bool bKeepConnection        = true;
    auto bOpenSessionInfoHeader = ImGui::CollapsingHeader(
            usprintf("%s##SessInfo", _key.c_str()),
            &bKeepConnection);
    ImGui::PopStyleColor(1);

    // Draw subwidget checkboxes
    {
        auto fnWrapCheckbox =
                [&](const char* label, bool* ptr) {
                    CPPH_CALL_ON_EXIT(ImGui::PopStyleColor(2));
                    if (*ptr)
                    {
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Button));
                    }

                    ImGui::SameLine();
                    ImGui::BeginDisabled(not IsSessionOpen());
                    if (ImGui::SmallButton(label)) { *ptr = !*ptr; }
                    ImGui::EndDisabled();
                };

        ImGui::SameLine(0, 10), ImGui::Text("");
        fnWrapCheckbox(" configs ", &_uiState.bConfigOpen);
        fnWrapCheckbox(" traces ", &_uiState.bTraceOpen);
        fnWrapCheckbox(" graphics ", &_uiState.bGraphicsOpen);
    }

    if (bOpenSessionInfoHeader)
        if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"SummaryGroup"}})
        {
            auto bRenderEntityContent = ShouldRenderSessionListEntityContent();
            auto width                = ImGui::GetContentRegionAvail().x / 2 * (bRenderEntityContent);
            if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"SessionState", width, false}})
            {
                ImGui::BulletText("Session State");
                ImGui::Separator();
            }

            if (bRenderEntityContent)
            {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, 0xff121212);
                if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"ConnectionInfo"}})
                {
                    ImGui::PopStyleColor();
                    ImGui::BulletText("Connection Control");
                    ImGui::Separator();
                    ImGui::Spacing();
                    RenderSessionListEntityContent();
                }
                else
                {
                    ImGui::PopStyleColor();
                }
            }
        }

    if (not bKeepConnection)
    {
        CloseSession();
    }

    if (CPPH_CALL_ON_EXIT(ImGui::EndChild()); ImGui::BeginChild("TerminalGroup", {}, true))
        drawTTY();
}

void BasicPerfkitNetClient::TickSession()
{
    if (not IsSessionOpen()) { return; }

    tickHeartbeat();
}

void BasicPerfkitNetClient::_onSessionCreate_(const msgpack::rpc::session_profile& profile)
{
    NotifyToast("Rpc Session Created").String(profile.peer_name);

    auto sesionInfo = decltype(service::session_info)::return_type{};
    auto result     = service::session_info(*_rpc).rpc(&sesionInfo, 1s);

    if (result != msgpack::rpc::rpc_status::okay)
    {
        NotifyToast{"Rpc invocation failed"}.Error().String(to_string(result));
        return;
    }

    auto ttyContent = decltype(service::fetch_tty)::return_type{};
    service::fetch_tty(*_rpc).rpc(&ttyContent, 0);

    PostEventMainThreadWeak(weak_from_this(),
                            [this,
                             info       = std::move(sesionInfo),
                             peer       = profile.peer_name,
                             ttyContent = std::move(ttyContent)]() mutable {
                                _sessionInfo  = std::move(info);

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
}

void BasicPerfkitNetClient::_onSessionDispose_(const msgpack::rpc::session_profile& profile)
{
    NotifyToast("Rpc Session Disposed").Wanrning().String(profile.peer_name);
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
                profile.peer_name,
                profile.total_read,
                profile.total_write));
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
    if (not _timHeartbeat.check_sparse()) { return; }

    if (_hrpcHeartbeat && not _hrpcHeartbeat.wait(0ms))
    {
        NotifyToast{"Heartbeat failed"}.Error();

        CloseSession();
        return;
    }

    auto onHeartbeat =
            [this](auto&& exception) {
                if (exception)
                    NotifyToast{"Heartbeat returned error"}
                            .Error()
                            .String(exception->what());
            };

    _hrpcHeartbeat = service::heartbeat(*_rpc).async_rpc(
            bind_front_weak(weak_from_this(), std::move(onHeartbeat)));
}

void BasicPerfkitNetClient::CloseSession()
{
    ISession::CloseSession();

    if (_hrpcHeartbeat) { _hrpcHeartbeat.abort(); }
}

void BasicPerfkitNetClient::drawTTY()
{
    struct TtyContext
    {
        perfkit::poll_timer timColorize{250ms};
        bool                bScrollLock   = false;
        int                 colorizeFence = 0;

        char                cmdBuf[512];

        float               uiControlPadHeight = 0;
    };
    auto& _              = RefAny<TtyContext>("TTY");
    bool  bFrameHasInput = false;

    // Retrieve buffer content
    _ttyQueue.access([&](string& str) {
        if (str.empty()) { return; }
        xterm_leap_escape(&_tty, str);
        str.clear();

        bFrameHasInput = true;
    });

    // When line exceeds maximum allowance ...
    if (auto ntot = _tty.GetTotalLines(); ntot > 17999)
    {
        auto lines = _tty.GetTextLines();
        lines.erase(lines.begin(), lines.begin() + 7999);

        _tty.SetTextLines(lines);
        _tty.ForceColorize(_tty.GetTotalLines() - 128, -128);
        _.colorizeFence = _tty.GetTotalLines();
    }

    // Apply colorization
    // Limited number of lines can be colorized at once
    if (_.timColorize.check_sparse())
        if (auto ntot = _tty.GetTotalLines(); ntot != _.colorizeFence)
        {
            _.colorizeFence = std::max(_.colorizeFence, ntot - 128);
            _tty.ForceColorize(_.colorizeFence - 1, -128);
            _.colorizeFence = _tty.GetTotalLines();
        }

    // Scroll Lock
    if (bFrameHasInput && not _.bScrollLock)
    {
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

        if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"ConfPanel"}})
        {
            ImGui::Checkbox("Scroll Lock", &_.bScrollLock);
            ImGui::SameLine();

            if (ImGui::Button(" clear "))
            {
                _.colorizeFence = 0;
                _tty.SetReadOnly(false);
                _tty.SelectAll();
                _tty.Delete();
                _tty.SetReadOnly(true);
            }

            ImGui::SetNextItemWidth(-1);
            ImGui::SameLine();
            ImGui::InputTextWithHint("##EnterCommand", "Enter Command Here", _.cmdBuf, sizeof _.cmdBuf);
        }

        _.uiControlPadHeight = ImGui::GetCursorPosY() - beginCursorPos;
    }
}
