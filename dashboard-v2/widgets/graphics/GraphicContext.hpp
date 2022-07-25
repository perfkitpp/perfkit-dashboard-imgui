//
// Created by ki608 on 2022-07-09.
//

#pragma once
#include "cpph/container/buffer.hxx"
#include "perfkit/remotegl/protocol/command.generic.hpp"

class GraphicContext
{
   public:
    void RenderContextPane(flex_buffer* dataToServer) {}
    void Tick(flex_buffer* dataToServer) {}
    void Dispose() {}

   public:
    void asyncRecvData(flex_buffer& data) {}
};
