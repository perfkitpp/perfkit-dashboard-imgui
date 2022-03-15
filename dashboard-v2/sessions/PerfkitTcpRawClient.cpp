//
// Created by ki608 on 2022-03-15.
//

#include "PerfkitTcpRawClient.hpp"

#include <asio/post.hpp>

#include "utils/Notify.hpp"

void PerfkitTcpRawClient::InitializeSession(const string& keyUri)
{
    _uri = keyUri;
}

bool PerfkitTcpRawClient::ShouldRenderSessionListEntityContent() const
{
    return true;
}

void PerfkitTcpRawClient::RenderSessionListEntityContent()
{
}

bool PerfkitTcpRawClient::IsSessionOpen() const
{
    return _state == EConnectionState::OnlineReadOnly
        || _state == EConnectionState::OnlineAdmin;
}

void PerfkitTcpRawClient::CloseSession()
{
    NotifyToast{}.AddString("Session Closed");
}

shared_ptr<ISession> CreatePerfkitTcpRawClient()
{
    return std::make_shared<PerfkitTcpRawClient>();
}
