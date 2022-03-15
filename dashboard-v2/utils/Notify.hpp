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
        int IdAllocated = -1;
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
    NotifyToast&& Title(string title) && { return _body->Title = std::move(title), _self(); }

    NotifyToast&& AddString(string content) &&;

    template <typename Fmt_, typename... Args_>
    NotifyToast&& AddString(Fmt_&& fmtstr, Args_&&... args) &&
    {
        return (std::move(*this))
                .AddString(fmt::format(std::forward<Fmt_>(fmtstr), std::forward<Args_>(args)...));
    }

    NotifyToast&& AddButton(function<void()> handler, string label = "Okay") &&;
    NotifyToast&& AddButton2(function<void()> onYes, function<void()> onNo, string labelYes = "Yes", string labelNo = "No") &&;

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
