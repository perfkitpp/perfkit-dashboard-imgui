//
// Created by ki608 on 2022-03-15.
//

#include "PerfkitTcpRawClient.hpp"

#include <charconv>

#include <asio/post.hpp>

#include "imgui-extension.h"

void PerfkitTcpRawClient::InitializeSession(const string& keyUri)
{
    _uri = keyUri;
}

bool PerfkitTcpRawClient::ShouldRenderSessionListEntityContent() const
{
    return _state != EConnectionState::OnlineAdmin;
}

void PerfkitTcpRawClient::RenderSessionListEntityContent()
{
    switch (_state)
    {
        case EConnectionState::Offline:
            if (ImGui::Button(usprintf("Connect##%s", _uri.c_str()), {-1, 0}))
            {
                if (_socket.is_open()) { _socket.close(); }

                _state = EConnectionState::Connecting;
                asio::post(bind_front_weak(weak_from_this(), &PerfkitTcpRawClient::startConnection, this));

                NotifyToast{}
                        .Permanent()
                        .String("Opening tcp raw client to [{}]", _uri)
                        .Custom([this] { return _state != EConnectionState::Connecting; });

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
    _socket.close();
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

    if (auto pos = uri.find_last_of(":"); pos == uri.npos)
    {
        NotifyToast{}.Error().String("Invalid URI: {}", uri);
        fnTransitTo(EConnectionState::Offline);
        return;
    }
    else
    {
        address         = string{uri.substr(0, pos)};
        auto portStr    = uri.substr(pos + 1);
        auto convResult = std::from_chars(portStr.data(), portStr.data() + portStr.size(), port);
        if (convResult.ec != std::errc{} || port > 65535)
        {
            NotifyToast{}.Error().String("Invalid URI (Port number parsing failed): {}", uri);
            fnTransitTo(EConnectionState::Offline);
            return;
        }
    }

    asio::ip::address addr;

    try
    {
        addr = asio::ip::make_address(address);
        // TODO: If failed to retrieve address, try resolve host via http
    }
    catch (asio::system_error&)
    {
        NotifyToast{}.Error().String("Failed to retrive IP: {}", address);
        fnTransitTo(EConnectionState::Offline);
        return;
    }

    _endpoint = {addr, uint16_t(port)};
    fnUpdateMessage("Connecting to [{}:{}]...", _endpoint.address().to_string(), _endpoint.port());

    try
    {
        _socket.open(_endpoint.protocol());
        _socket.connect(_endpoint);

        NotifyToast{}.String("Connection to session [{}] successfully established.", _uri);
        fnTransitTo(EConnectionState::OnlineReadOnly);
    }
    catch (asio::system_error& ec)
    {
        NotifyToast{}.Error().String("Connection failed: {}", ec.code().value());
        fnTransitTo(EConnectionState::Offline);
    }
}

shared_ptr<ISession> CreatePerfkitTcpRawClient()
{
    return std::make_shared<PerfkitTcpRawClient>();
}
