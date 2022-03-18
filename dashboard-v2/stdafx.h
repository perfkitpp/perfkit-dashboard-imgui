#pragma once
#include <any>
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

template <class Owner_, typename Callable_>
void PostEventMainThreadWeak(Owner_&& weakPtr, Callable_&& callable)
{
    PostEventMainThread(bind_front_weak(
            weakPtr, std::forward<Callable_>(callable)));
}

namespace detail {

size_t RetrieveCurrentWindowName(char* buffer, int buflen);

template <typename... Args_>
string_view MkStrView(Args_&&... args)
{
    static char buf[256];
    auto        n = RetrieveCurrentWindowName(buf, sizeof buf);
    n += snprintf(buf + n, sizeof buf - n, std::forward<Args_>(args)...);

    return string_view{buf, n};
}

void      GetVar(string_view name, int** dst);
void      GetVar(string_view name, float** dst);
void      GetVar(string_view name, bool** dst);
void      GetVar(string_view name, string** dst);

std::any& GetAny(string_view name);
}  // namespace detail

/**
 * Should only be called inside of main thread !!
 */
template <typename Type_, typename Fmt_, typename... Args_>
Type_& RefVar(Fmt_&& format, Args_&&... args)
{
    Type_* ptr;
    detail::GetVar(detail::MkStrView(format, std::forward<Args_>(args)...), &ptr);
    return *ptr;
}

template <typename Type_, typename Fmt_, typename... Args_>
Type_& RefAny(Fmt_&& format, Args_&&... args)
{
    std::any& any    = detail::GetAny(detail::MkStrView(format, std::forward<Args_>(args)...));
    auto      result = std::any_cast<Type_>(&any);

    if (not result)
    {
        any    = Type_{};
        result = std::any_cast<Type_>(&any);
    }

    return *result;
}

#include "utils/Notify.hpp"
