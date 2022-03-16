#pragma once
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <perfkit/common/functional.hxx>
#include <perfkit/common/futils.hxx>

using perfkit::bind_front;
using perfkit::bind_front_weak;
using perfkit::function;
using perfkit::futils::usprintf;

using std::shared_ptr;
using std::string;
using std::string_view;
using std::unique_ptr;
using std::vector;
using std::weak_ptr;
using std::chrono::steady_clock;

using namespace std::literals;

namespace detail {
template <typename Callable_>
struct ConditionalFinalInvoke
{
    bool      cond;
    Callable_ callable;

    ~ConditionalFinalInvoke() noexcept(std::is_nothrow_invocable_v<Callable_>)
    {
        if (cond) { callable(); }
    }

    operator bool() const noexcept { return cond; }
};

template <typename Callable>
auto CondInvoke(bool condition, Callable&& callable)
{
    return detail::ConditionalFinalInvoke<Callable>{condition, std::forward<Callable>(callable)};
}
}  // namespace detail

#define INTERNAL_PDASH_CONCAT_0(a, b) a##b
#define INTERNAL_PDASH_CONCAT(a, b)   INTERNAL_PDASH_CONCAT_0(a, b)
#define CondInvoke(CondExpr, ...)     auto INTERNAL_PDASH_CONCAT(_pdash_intennal_, __LINE__) = detail::CondInvoke(CondExpr, __VA_ARGS__)

void DispatchEventMainThread(function<void()>);
void PostEventMainThread(function<void()>);

#include "utils/Notify.hpp"
