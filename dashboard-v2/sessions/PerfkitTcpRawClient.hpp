//
// Created by ki608 on 2022-03-15.
//

#pragma once
#include "BasicPerfkitNetClient.hpp"

class PerfkitTcpRawClient : public BasicPerfkitNetClient
{
    string _uri;

   public:
    void InitializeSession(const string& keyUri) override;
    bool ShouldRenderSessionListEntityContent() const override;
    void RenderSessionListEntityContent() override;
    bool IsSessionOpen() const override;
    void CloseSession() override;
};

shared_ptr<ISession> CreatePerfkitTcpRawClient();
