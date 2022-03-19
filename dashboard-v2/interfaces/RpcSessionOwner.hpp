#pragma once
#include <memory>

namespace perfkit::msgpack::rpc {
class context;
}

class IRpcSessionOwner
{
   public:
    virtual ~IRpcSessionOwner() noexcept = default;

   public:
    virtual auto RpcContext() -> perfkit::msgpack::rpc::context* = 0;
    virtual auto SessionAnchor() -> weak_ptr<void> = 0;
};
