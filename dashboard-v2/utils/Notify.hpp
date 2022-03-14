//
// Created by ki608 on 2022-03-14.
//

#pragma once

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

   private:
    NotifySeverity _severity = NotifySeverity::Info;
    string _title;
    vector<function<bool()>> _contentDecos;  // returns true if toast should be closed
    steady_clock::time_point _lifespan = steady_clock::now() + 3s;
    steady_clock::time_point _spawned;
    int _idAlloc = -1;

   public:
    NotifyToast() noexcept              = default;
    NotifyToast(NotifyToast&&) noexcept = default;
    NotifyToast& operator=(NotifyToast&&) noexcept = default;
    NotifyToast(NotifyToast const&) noexcept       = delete;
    NotifyToast& operator=(NotifyToast const&) noexcept = delete;

   public:
    NotifyToast&& Severity(NotifySeverity value) && { return _severity = value, _self(); }
    NotifyToast&& Title(string title) && { return _title = std::move(title), _self(); }
    void Commit() &&;

    NotifyToast&& AddString(string content) &&;
    NotifyToast&& AddButton(function<void()> handler, string label = "Okay") &&;
    NotifyToast&& AddButton2(function<void()> onYes, function<void()> onNo, string labelYes = "Yes", string labelNo = "No") &&;

    template <typename Duration_>
    NotifyToast&& Lifespan(Duration_&& dur) &&
    {
        _lifespan = steady_clock::now() + dur;
        return _self();
    }

   private:
    NotifyToast&& _self() { return std::move(*this); }
};

void RenderNotifies();
