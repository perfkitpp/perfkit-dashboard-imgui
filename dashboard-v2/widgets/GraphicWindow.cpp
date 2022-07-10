//
// Created by ki608 on 2022-07-09.
//

#include "GraphicWindow.hpp"

#include "cpph/refl/object.hxx"
#include "cpph/refl/rpc.hxx"
#include "graphics/GraphicContext.hpp"
#include "imgui.h"
#include "imgui_extension.h"
#include "imgui_impl_opengl3_loader.h"
#include "perfkit/extension/net/protocol.hpp"

namespace proto = perfkit::net::message;

void widgets::GraphicWindow::Tick()
{
    // if(_context) { _context->flush(); }
}

auto constexpr POPUP_PORMPT_FORCE_EXEC = "##PromptForceExec";

void widgets::GraphicWindow::Render()
{
    ImGui::TextUnformatted(KEYTEXT(Graphics, " Graphics"));
    ImGui::Separator();

    if (not _host->RpcSession()) {
        ImGui::BeginDisabled();

        ImGui::PushFontScale(2);
        ImGui::Button(KEYTEXT(OFFLINE, "-- OFFLINE --"), {-1, -1});
        ImGui::PopFontScale();

        ImGui::EndDisabled();

        return;
    }

    if (not _context) {
        auto bAlreadyConnecting = !!_hActiveConnectRequest;
        if (bAlreadyConnecting) { ImGui::BeginDisabled(); }

        ImGui::PushFontScale(2);
        auto bStartConnection = ImGui::Button(bAlreadyConnecting ? KEYTEXT(CONNECTING, "CONNECTING ...") : LOCWORD("CONNECT"), {-1, -1});
        ImGui::PopFontScale();

        if (bAlreadyConnecting) { ImGui::EndDisabled(); }

        if (bStartConnection) {
            auto reqResult = new bool{};
            auto stub = proto::service::graphics_take_control(_host->RpcSession());
            _hActiveConnectRequest = stub.async_request(
                    reqResult, true,
                    [this,
                     reqResult = unique_ptr<bool>{reqResult},
                     anchor = _host->SessionAnchor()]  //
                    (rpc::error_code ec, string_view errstr) {
                        if (ec) {
                            NotifyToast{LOCTEXT("RPC Invocation Failed: {}"), errstr}.Error();
                        } else if (*reqResult) {
                            PostEventMainThreadWeak(anchor, [this] {
                                // TODO: Create context ...
                            });
                        } else if (CPPH_TMPVAR = anchor.lock()) {
                            // Open retry modal
                            PostEventMainThreadWeak(anchor, [this, anchor] {
                                NotifyToast{LOCTEXT("({})\nGraphics: Access denied"), _host->DisplayString()}
                                        .String(KEYTEXT(
                                                ERROR_GRAHPICS_SESSION_ACCESS_DENIED,
                                                "Another session is already occupying graphics context. "
                                                "Continuing this operation may cause another remote client"
                                                "to be disconnected from graphics session. \n"
                                                "Continue?"))
                                        .Warning()
                                        .Permanent()
                                        .ButtonYesNo([this] { _forceConnect(); }, LOCWORD("  YES  "), default_function, LOCWORD("  NO  "))
                                        .OnClosed([this] { _hActiveConnectRequest.reset(); })
                                        .Custom([anchor] { return not anchor.expired(); });
                            });

                            return;
                        }

                        PostEventMainThreadWeak(anchor, [this] { _hActiveConnectRequest.reset(); });
                    });
        }
    }
}

widgets::GraphicWindow::GraphicWindow(IRpcSessionOwner* host) : _host(host) {}
widgets::GraphicWindow::~GraphicWindow() = default;
