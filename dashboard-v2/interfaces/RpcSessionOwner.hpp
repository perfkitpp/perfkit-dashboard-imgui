#pragma once
#include <memory>

namespace perfkit::rpc {
class session;
}

class IRpcSessionOwner
{
   public:
    virtual ~IRpcSessionOwner() noexcept = default;

   public:
    virtual auto KeyString() const -> string const& = 0;
    virtual auto DisplayString() const -> string const& = 0;

    virtual auto RpcSession() -> perfkit::rpc::session* = 0;
    virtual auto SessionAnchor() -> weak_ptr<void> = 0;
};
