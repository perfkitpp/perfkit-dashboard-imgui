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
    void RenderTickWindow() {}
    void ClearContexts() {}

   public:
    void HandleNewConfigClass(string const& key, notify::config_category_t const& root) {}
    void HandleConfigUpdate(string const& key, config_entity_update_t const& entity) {}
};
}  // namespace widgets
