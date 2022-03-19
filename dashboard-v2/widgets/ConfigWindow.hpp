//
// Created by ki608 on 2022-03-18.
//

#pragma once
#include "interfaces/RpcSessionOwner.hpp"
#include "perfkit/extension/net/protocol.hpp"

namespace widgets {
using namespace perfkit;
using namespace perfkit::net::message;

class ConfigWindow
{
    IRpcSessionOwner* _host;
    weak_ptr<void>    _sessionAnchor;

   public:
    explicit ConfigWindow(IRpcSessionOwner* host) noexcept : _host(host) {}

   public:
    void TickWindow() {}
    void RenderTickWindow(bool* bKeepOpen) {}
    void ClearContexts() {}

   public:
    void HandleNewConfigClass(string const&) {}
};
}  // namespace widgets
