//
// Created by ki608 on 2022-03-14.
//

#pragma once
#include <optional>

#include <spdlog/fmt/fmt.h>

enum class NotifySeverity
{
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
    struct Content
    {
        NotifySeverity Severity = NotifySeverity::Info;
        string Title;
        vector<function<bool()>> ContentDecos;  // returns true if toast should be closed
        steady_clock::time_point Lifespan = steady_clock::now() + 5s;
        steady_clock::time_point Birth;

        bool bInfinity          = false;
        int stateIdAlloc        = -1;
        float stateHeightOffset = 0.f;
        float toastHeightCache  = 0.f;
    };

   private:
    std::optional<Content> _body = Content{};

   public:
    NotifyToast() noexcept              = default;
    NotifyToast(NotifyToast&&) noexcept = default;
    NotifyToast& operator=(NotifyToast&&) noexcept = default;
    NotifyToast(NotifyToast const&) noexcept       = delete;
    NotifyToast& operator=(NotifyToast const&) noexcept = delete;

    ~NotifyToast();

   public:
    void Commit() &&;

    NotifyToast&& Severity(NotifySeverity value) && { return _body->Severity = value, _self(); }
    NotifyToast&& Trivial() && { return std::move(*this).Severity(NotifySeverity::Trivial); }
    NotifyToast&& Wanrning() && { return std::move(*this).Severity(NotifySeverity::Warning); }
    NotifyToast&& Error() && { return std::move(*this).Severity(NotifySeverity::Error); }
    NotifyToast&& Fatal() && { return std::move(*this).Severity(NotifySeverity::Fatal); }

    NotifyToast&& Infinity() && { return _body->bInfinity = true, _self(); }

    NotifyToast&& Title(string title) && { return _body->Title = std::move(title), _self(); }

    NotifyToast&& String(string content) &&;

    template <typename Fmt_, typename... Args_>
    NotifyToast&& String(Fmt_&& fmtstr, Args_&&... args) &&
    {
        return (std::move(*this))
                .String(fmt::format(std::forward<Fmt_>(fmtstr), std::forward<Args_>(args)...));
    }

    NotifyToast&& Button(function<void()> handler, string label = "Okay") &&;
    NotifyToast&& ButtonYesNo(function<void()> onYes, function<void()> onNo, string labelYes = "Yes", string labelNo = "No") &&;

    NotifyToast&& Custom(function<bool()> handler) &&;

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
