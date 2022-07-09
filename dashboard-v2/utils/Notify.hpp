//
// Created by ki608 on 2022-03-14.
//

#pragma once
#include <optional>

#include <spdlog/fmt/fmt.h>

enum class NotifySeverity {
    Trivial,
    Info,
    Warning,
    Error,
    Fatal
};

class NotifyToast
{
    friend class NotifyContext;

   public:
    struct Content {
        NotifySeverity Severity = NotifySeverity::Info;
        vector<ufunction<bool()>> ContentDecos;  // returns true if toast should be closed
        steady_clock::time_point Lifespan = steady_clock::now() + 5s;
        steady_clock::time_point Birth;

        ufunction<void()> OnForceClose = cpph::default_function;

        bool bInfinity = false;
        int stateIdAlloc = -1;
        float stateHeightOffset = 0.f;
        float toastHeightCache = 0.f;
        bool stateHovering = false;
    };

   private:
    unique_ptr<Content> _body{new Content{}};

   public:
    NotifyToast() noexcept = default;

    template <typename Fmt_, typename... Args_>
    NotifyToast&& Title(Fmt_&& fmt, Args_&&... args) &&
    {
        return std::move(*this).Title(
                fmt::format(std::forward<Fmt_>(fmt), std::forward<Args_>(args)...));
    }

    template <typename Fmt_, typename... Args_>
    explicit NotifyToast(Fmt_&& fmt, Args_&&... args) noexcept
    {
        std::move(*this).Title(std::forward<Fmt_>(fmt), std::forward<Args_>(args)...);
    }

    NotifyToast(NotifyToast&&) noexcept = default;
    NotifyToast& operator=(NotifyToast&&) noexcept = default;
    NotifyToast(NotifyToast const&) noexcept = delete;
    NotifyToast& operator=(NotifyToast const&) noexcept = delete;

    ~NotifyToast();

   public:
    NotifyToast&& OnForceClose(ufunction<void()> fn) && { return _body->OnForceClose = std::move(fn), _self(); }

    NotifyToast&& Severity(NotifySeverity value) && { return _body->Severity = value, _self(); }
    NotifyToast&& Trivial() && { return std::move(*this).Severity(NotifySeverity::Trivial); }
    NotifyToast&& Wanrning() && { return std::move(*this).Severity(NotifySeverity::Warning); }
    NotifyToast&& Error() && { return std::move(*this).Severity(NotifySeverity::Error); }
    NotifyToast&& Fatal() && { return std::move(*this).Severity(NotifySeverity::Fatal); }

    NotifyToast&& Permanent() && { return _body->bInfinity = true, _self(); }

    NotifyToast&& Separate() &&;
    NotifyToast&& String(string content) &&;

    NotifyToast&& Spinner() &&;

    template <typename Fmt_, typename... Args_>
    NotifyToast&& String(Fmt_&& fmtstr, Args_&&... args) &&
    {
        return (std::move(*this))
                .String(fmt::format(std::forward<Fmt_>(fmtstr), std::forward<Args_>(args)...));
    }

    NotifyToast&& Title(string content) &&;

    NotifyToast&& Button(ufunction<void()> handler, string label = "Okay") &&;
    NotifyToast&& ButtonYesNo(ufunction<void()> onYes, ufunction<void()> onNo, string labelYes = "Yes", string labelNo = "No") &&;

    NotifyToast&& Custom(ufunction<bool()> handler) &&;

    template <typename Duration_>
    NotifyToast&& Lifespan(Duration_&& dur) &&
    {
        _body->Lifespan = steady_clock::now() + dur;
        return _self();
    }

   private:
    NotifyToast&& _self() { return std::move(*this); }
};

void RenderNotifies();
