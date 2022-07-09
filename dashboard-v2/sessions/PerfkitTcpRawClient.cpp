//
// Created by ki608 on 2022-03-15.
//

#include "PerfkitTcpRawClient.hpp"

#include <charconv>

#include <asio/post.hpp>
#include <cpph/refl/rpc/connection/asio.hxx>
#include <cpph/refl/rpc/rpc.hxx>

#include "imgui_extension.h"

using namespace perfkit;

void PerfkitTcpRawClient::InitializeSession(const string& keyUri)
{
    BasicPerfkitNetClient::InitializeSession(keyUri);
    _uri = keyUri;
}

void PerfkitTcpRawClient::RenderSessionOpenPrompt()
{
    switch (_state) {
        case EConnectionState::Offline:
            if (ImGui::Button(usprintf("Connect##%s", _uri.c_str()), {-1, 0})) {
                _sockPtrConnecting.store(nullptr);

                _state = EConnectionState::Connecting;
                asio::post(bind_front_weak(weak_from_this(), &PerfkitTcpRawClient::startConnection, this));

                NotifyToast{"Now Connecting ..."}
                        .Permanent()
                        .Spinner()
                        .String("(TCP_RAW) {}", _uri)
                        .Custom([this, self = weak_from_this()] { return not self.expired() && _state == EConnectionState::Connecting; })
                        .OnForceClose([this] {
                            auto ptr = _sockPtrConnecting.exchange(nullptr);
                            if (not ptr) { return; }

                            ptr->close();
                            _sockPtrConnecting.exchange(ptr);
                        });

                _uiStateMessage = fmt::format("Connecting to [{}] ...", _uri);
            }
            break;

        case EConnectionState::Connecting:
            ImGui::Spinner("##Connecting", 0xffba8a3c);
            ImGui::SameLine();
            ImGui::TextUnformatted(_uiStateMessage.c_str());
            break;

        case EConnectionState::OnlineReadOnly:
            ImGui::Text("TODO: Enter ID");
            ImGui::Text("TODO: Enter Password");
            break;

        case EConnectionState::OnlineAdmin:
            break;
    }
}

bool PerfkitTcpRawClient::IsSessionOpen() const
{
    return _state == EConnectionState::OnlineReadOnly
        || _state == EConnectionState::OnlineAdmin;
}

void PerfkitTcpRawClient::CloseSession()
{
    BasicPerfkitNetClient::CloseSession();
    _state = EConnectionState::Offline;
}

void PerfkitTcpRawClient::startConnection()
{
    using asio::ip::tcp;
    auto fnUpdateMessage =
            [&](auto&&... args) {
                PostEventMainThread([this, str = fmt::format(args...)] { _uiStateMessage = str; });
            };

    auto fnTransitTo =
            [&](EConnectionState nextState) {
                PostEventMainThread([this, nextState] { _state = nextState; });
            };

    string_view uri = _uri;
    string address;
    int port;

    if (auto pos = uri.find_last_of(':'); pos == uri.npos) {
        NotifyToast{"Invalid URI"}.Error().String("Colon not found '{}'", uri);
        fnTransitTo(EConnectionState::Offline);
        return;
    } else {
        address = string{uri.substr(0, pos)};
        auto portStr = uri.substr(pos + 1);
        auto convResult = std::from_chars(portStr.data(), portStr.data() + portStr.size(), port);
        if (convResult.ec != std::errc{} || port > 65535) {
            NotifyToast{"Invalid URI"}.Error().String("Port number parsing failed: {}", portStr);
            fnTransitTo(EConnectionState::Offline);
            return;
        }
    }

    asio::ip::address addr;

    try {
        addr = asio::ip::make_address(address);
        // TODO: If failed to retrieve address, try resolve host via http
    } catch (asio::system_error& ec) {
        NotifyToast{"Connection Failed"}
                .Error()
                .String("Retriving IP failed - '{}'", address)
                .String(">> ERROR {}", ec.code().value());
        fnTransitTo(EConnectionState::Offline);
        return;
    }

    _endpoint = {addr, uint16_t(port)};
    fnUpdateMessage("Connecting to [{}:{}]...", _endpoint.address().to_string(), _endpoint.port());

    try {
        tcp::socket sock{_exec};
        sock.open(_endpoint.protocol());

        _sockPtrConnecting.exchange(&sock);
        sock.connect(_endpoint);
        while (nullptr == _sockPtrConnecting.exchange(nullptr)) { std::this_thread::sleep_for(10ms); }
        if (not sock.is_open()) { throw std::runtime_error{"User aborted connection"}; }

        NotifyToast{"Connected"}.String("Connection to session [{}] successfully established.", _uri);
        fnTransitTo(EConnectionState::OnlineReadOnly);

        auto conn = make_unique<rpc::asio_stream<tcp>>(std::move(sock));
        this->NotifyNewConnection(std::move(conn));
    } catch (asio::system_error& ec) {
        NotifyToast{"Connection Failed"}.Error().String(">> ERROR {}", ec.code().value());
        fnTransitTo(EConnectionState::Offline);
    } catch (std::exception& ec) {
        NotifyToast{"Connection Aborted"}.Error().String(">> ERROR {}", ec.what());
    }
}

shared_ptr<ISession> CreatePerfkitTcpRawClient()
{
    return std::make_shared<PerfkitTcpRawClient>();
}
