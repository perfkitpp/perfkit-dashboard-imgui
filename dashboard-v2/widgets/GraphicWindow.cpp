//
// Created by ki608 on 2022-07-09.
//

#include "GraphicWindow.hpp"

#include "cpph/refl/object.hxx"
#include "cpph/refl/rpc.hxx"
#include "cpph/refl/rpc/service.hxx"
#include "cpph/refl/types/binary.hxx"
#include "graphics/GraphicContext.hpp"
#include "imgui.h"
#include "imgui_extension.h"
#include "imgui_impl_opengl3_loader.h"
#include "perfkit/extension/net/protocol.hpp"

namespace proto = perfkit::net::message;
widgets::GraphicWindow::GraphicWindow(IRpcSessionOwner* host) : _host(host) {}

void widgets::GraphicWindow::BuildService(rpc::service_builder& S)
{
    S.route(proto::notify::graphics_init, [this] { _asyncInitGraphics(); });
    S.route(proto::service::graphics_send_data, [this](cpph::flex_buffer& data) { _asyncRecvData(data); });
    S.route(proto::notify::graphics_control_lost, [this] { _asyncDeinitGraphics(); });
}

void widgets::GraphicWindow::Tick(bool* pEnableState)
{
    bool const bHasChange = exchange(_bPreviousEnableState, *pEnableState) != *pEnableState;

    if (not _host->RpcSession()) {
        *pEnableState = false;
        return;
    }

    if (exchange(_bDisconnected, false)) {
        _context.reset();
        *pEnableState = false;
    }

    if (bHasChange && *pEnableState && not _hActiveConnectRequest && _context.expired()) {
        auto reqResult = new bool{};
        auto stub = proto::service::graphics_take_control(_host->RpcSession());
        _hActiveConnectRequest = stub.async_request(
                reqResult, true,
                [this, pEnableState,
                 reqResult = unique_ptr<bool>{reqResult},
                 anchor = _host->SessionAnchor()]  //
                (rpc::error_code ec, string_view errstr) {
                    bool bNextEnableState = false;
                    if (ec) {
                        NotifyToast{LOCTEXT("RPC Invocation Failed: {}"), errstr}.Error();
                    } else if (*reqResult) {
                        bNextEnableState = true;
                    } else if (CPPH_TMPVAR = anchor.lock()) {
                        // Open retry modal
                        PostEventMainThreadWeak(anchor, [this, anchor, pEnableState] {
                            NotifyToast{KEYTEXT(GRAHPICS_ACCESS_DENIED, "{})\nGraphics: Access denied"), _host->DisplayString()}
                                    .String(KEYTEXT(
                                            ERROR_GRAHPICS_SESSION_ACCESS_DENIED,
                                            "Another session is already occupying graphics context. "
                                            "Continuing this operation may cause another remote client"
                                            "to be disconnected from graphics session. \n"
                                            "Continue?"))
                                    .Warning()
                                    .Permanent()
                                    .ButtonYesNo([this, pEnableState] { *pEnableState = _forceConnect(); },
                                                 LOCWORD("  YES  "),
                                                 [this, pEnableState] { *pEnableState = false; },
                                                 LOCWORD("  NO  "))
                                    .OnClosed([this] { _hActiveConnectRequest.reset(); })
                                    .Custom([anchor, pEnableState] { return *pEnableState && not anchor.expired(); });
                        });

                        return;
                    }

                    PostEventMainThreadWeak(anchor, [this, pEnableState, bNextEnableState] {
                        _hActiveConnectRequest.reset();
                        *pEnableState = bNextEnableState;
                    });
                });
    }

    if (bHasChange && not *pEnableState && not _context.expired()) {
        proto::service::graphics_release_control(_host->RpcSession()).notify();
        _context.reset();

        *pEnableState = false;
    }

    if (auto ctx = _context.lock()) {
        ctx->Tick(&_dataToServer);
        _commitData(&_dataToServer);
    }
}

auto constexpr POPUP_PORMPT_FORCE_EXEC = "##PromptForceExec";

void widgets::GraphicWindow::Render()
{
    ImGui::TextUnformatted(KEYTEXT(Graphics, " Graphics"));
    ImGui::Separator();

    if (_hActiveConnectRequest) {
        ImGui::BeginDisabled(true);
        ImGui::Button(LOCTEXT("CONNECTING ..."), {-1, -1});
        ImGui::EndDisabled();
    }

    if (auto ctx = _context.lock()) {
        ctx->RenderContextPane(&_dataToServer);
        _commitData(&_dataToServer);
    }
}

bool widgets::GraphicWindow::_forceConnect()
{
    rpc::error_code ec;
    auto stub = proto::service::graphics_take_control(_host->RpcSession());
    auto result = stub.request(false, 1s, ec);

    if (ec || not result) {
        NotifyToast{KEYTEXT(GRAHPICS_FORCECONN_FAILED, "{}) Force Connection has failed."), _host->DisplayString()}
                .String(ec ? ec.message() : KEYWORD(ERROR_UNKNOWN))
                .Error();

        return false;
    }

    return true;
}

void widgets::GraphicWindow::_asyncInitGraphics()
{
    if (_async.context) {
        PostEventMainThread([this, context = exchange(_async.context, {})] {
            context->Dispose();  // Cleanup OpenGL buffers, etc ...
        });
    }

    _async.context = make_shared<GraphicContext>();
    PostEventMainThread([this, context = _async.context] { _context = context; });
}

void widgets::GraphicWindow::_asyncRecvData(flex_buffer& data)
{
    _async.context->asyncRecvData(data);
}

void widgets::GraphicWindow::_asyncDeinitGraphics()
{
    if (_async.context) {
        PostEventMainThread([this, context = exchange(_async.context, {})] {
            _bDisconnected = true;
            context->Dispose();  // Cleanup OpenGL buffers, etc ...
        });
    }
}

void widgets::GraphicWindow::_commitData(flex_buffer*)
{
    if (not _dataToServer.empty()) {
        // TODO: Make this async, to avoid system call inside of primary loop ...
        proto::service::graphics_send_data(_host->RpcSession()).notify(_dataToServer);
        _dataToServer.clear();
    }
}

widgets::GraphicWindow::~GraphicWindow()
{
    if (_async.context) {
        // OpenGL context data must be disposed!
        _async.context->Dispose();
    }
}
