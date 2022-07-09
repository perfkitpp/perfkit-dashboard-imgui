//
// Created by ki608 on 2022-07-09.
//

#pragma once
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

   public:
    explicit GraphicWindow(IRpcSessionOwner* host);
    ~GraphicWindow();

   public:
    void BuildService(rpc::service_builder&) {}
    void Tick();
    void Render();
};
}  // namespace widgets
