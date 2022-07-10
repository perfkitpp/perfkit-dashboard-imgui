//
// Created by ki608 on 2022-07-09.
//

#pragma once
#include "cpph/container/buffer.hxx"
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
    weak_ptr<GraphicContext> _context;

    rpc::request_handle _hActiveConnectRequest;
    bool _bPreviousEnableState = false;
    bool _bDisconnected = false;

    struct AsyncContext {
        shared_ptr<GraphicContext> context;
    } _async;

   public:
    explicit GraphicWindow(IRpcSessionOwner* host);
    ~GraphicWindow();

   public:
    void BuildService(rpc::service_builder&);
    void Tick(bool* bEnableState);
    void Render();

   public:
    bool _forceConnect();

   public:
    void _asyncInitGraphics();
    void _asyncRecvData(cpph::flex_buffer&);
    void _asyncDeinitGraphics();
};
}  // namespace widgets
