//
// Created by ki608 on 2022-07-09.
//

#pragma once
#include "cpph/refl/rpc/core.hxx"
#include "interfaces/RpcSessionOwner.hpp"

class GraphicContext;

namespace cpph::rpc {
class service_builder;
}

namespace widgets {
/**
 * - Graphics context management
 * - I/O Management
 */
class GraphicWindow
{
   private:
    IRpcSessionOwner* _host;
    ptr<GraphicContext> _context;

    rpc::request_handle _hActiveConnectRequest;

   public:
    explicit GraphicWindow(IRpcSessionOwner* host);
    ~GraphicWindow();

   public:
    void BuildService(rpc::service_builder&) {}
    void Tick();
    void Render();

   public:
    void _forceConnect() {}
    void _createGraphics() {}
    void _clearGrahpics() {}
};
}  // namespace widgets
