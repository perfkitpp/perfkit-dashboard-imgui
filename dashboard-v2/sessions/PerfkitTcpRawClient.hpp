//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include <asio/ip/tcp.hpp>
#include <asio/system_executor.hpp>

#include "BasicPerfkitNetClient.hpp"

class PerfkitTcpRawClient : public BasicPerfkitNetClient
{
    enum class EConnectionState
    {
        Offline,
        Connecting,
        OnlineReadOnly,
        OnlineAdmin,
    };

   private:
    string                  _uri;    // Key URI
    EConnectionState        _state;  // Current connection state

    asio::system_executor   _exec;      // System executor
    asio::ip::tcp::endpoint _endpoint;  // Active endpoint

    string                  _uiStateMessage;  // UI State

   public:
    ~PerfkitTcpRawClient()
    {
        _exec.context().stop();
        _exec.context().join();
    }

   public:
    void InitializeSession(const string& keyUri) override;
    bool ShouldRenderSessionListEntityContent() const override;
    void RenderSessionListEntityContent() override;
    bool IsSessionOpen() const override;
    void CloseSession() override;

   private:
    void startConnection();
};

shared_ptr<ISession> CreatePerfkitTcpRawClient();
