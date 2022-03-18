//
// Created by ki608 on 2022-03-15.
//

#include "BasicPerfkitNetClient.hpp"

#include <asio/dispatch.hpp>
#include <asio/post.hpp>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/refl/msgpack-rpc/context.hxx>

#include "imgui_extension.h"

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
    service.serve(notify::tty,
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
    auto constexpr HEADER_FLAGS
            = ImGuiTreeNodeFlags_NoTreePushOnOpen
            | ImGuiTreeNodeFlags_FramePadding
            | ImGuiTreeNodeFlags_SpanFullWidth;

    // Basic buttons for opening config/trace
    // if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"ButtonsPanel"}})
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
    drawButtonsPanel();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);

    // State summary (bandwidth, memory usage, etc ...)
    ImGui::PushStyleColor(ImGuiCol_Header, IsSessionOpen() ? 0xff'257d47 : ImGui::GetColorU32(ImGuiCol_Header));
    bool bKeepConnection = true;

    if (ImGui::CollapsingHeader(usprintf("%s###SessInfo", _key.c_str()), &bKeepConnection))
        if (CPPH_TMPVAR{ImGui::ScopedChildWindow{"SummaryGroup"}})
        {
            if (ShouldRenderSessionListEntityContent())
            {
                CPPH_TMPVAR{ImGui::ScopedChildWindow{"ConnectionInfo"}};
                RenderSessionListEntityContent();
                ImGui::Spacing();
            }

            ImGui::Text("Session State Here");
        }

    if (not bKeepConnection)
    {
        CloseSession();
    }

    ImGui::PopStyleColor();

    // List of available GUI windows
    if (ImGui::TreeNodeEx("Windows", HEADER_FLAGS))
        ;

    // TTY
    if (ImGui::TreeNodeEx("Terminal", ImGuiTreeNodeFlags_DefaultOpen | HEADER_FLAGS))
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
                "    PEER     : {}\n"
                "    Rx       : {:<24L} bytes\n"
                "    Tx       : {:<24L} bytes\n"
                "</eof>\n"
                "\n\n",
                profile.peer_name,
                profile.total_read,
                profile.total_write));
    });
    CloseSession();
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
        _tty.SetReadOnly(false);
        _tty.AppendTextAtEnd(str.c_str());
        _tty.SetReadOnly(true);
        str.clear();

        bFrameHasInput = true;
    });

    if (bFrameHasInput)
    {
        // When line exceeds maximum allowance ...
        if (auto ntot = _tty.GetTotalLines(); ntot > 17999)
        {
            auto lines = _tty.GetTextLines();
            lines.erase(lines.begin(), lines.begin() + 7999);

            _tty.SetTextLines(std::move(lines));
            _tty.ForceColorize(_tty.GetTotalLines() - 128, -1);
            _.colorizeFence = _tty.GetTotalLines();
        }

        // Apply colorization
        // Limited number of lines can be colorized at once
        if (_.timColorize.check_sparse())
            if (auto ntot = _tty.GetTotalLines(); ntot != _.colorizeFence)
            {
                _.colorizeFence = std::max(_.colorizeFence, ntot - 128);
                _tty.ForceColorize(_.colorizeFence - 1);
                _.colorizeFence = _tty.GetTotalLines();
            }

        // Scroll Lock
        if (not _.bScrollLock)
        {
            _tty.MoveBottom();
            _tty.MoveEnd();
            auto xscrl = ImGui::GetScrollX();
            ImGui::SetScrollY(ImGui::GetScrollMaxY());
            ImGui::SetScrollX(xscrl);
        }
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

            if (ImGui::Button("Clear All"))
            {
                _.colorizeFence = 0;
                _tty.SetReadOnly(false);
                _tty.SelectAll();
                _tty.Delete();
                _tty.SetReadOnly(true);
            }
        }

        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##EnterCommand", "Enter Command Here", _.cmdBuf, sizeof _.cmdBuf);
        _.uiControlPadHeight = ImGui::GetCursorPosY() - beginCursorPos;
    }
}

void BasicPerfkitNetClient::drawButtonsPanel()
{
    ImGui::Checkbox("Config Window", &_uiState.bConfigOpen);
    ImGui::SameLine(), ImGui::Checkbox("Trace Window", &_uiState.bTraceOpen);
}
