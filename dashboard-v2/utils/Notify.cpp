//
// Created by ki608 on 2022-03-14.
//

#include "Notify.hpp"

#include <list>
#include <map>
#include <mutex>

#include <perfkit/common/futils.hxx>
#include <perfkit/common/macros.hxx>
#include <perfkit/common/utility/cleanup.hxx>
#include <spdlog/spdlog.h>

#include "Application.hpp"
#include "imgui.h"
#include "imgui_extension.h"

static class NotifyContext
{
    std::list<unique_ptr<NotifyToast::Content>>                     _queue;
    std::mutex                                                      _mtxQueue;

    std::list<unique_ptr<NotifyToast::Content>>                     _toasts;
    std::map<steady_clock::time_point, decltype(_toasts)::iterator> _timeouts;

    vector<int>                                                     _idPool;

    shared_ptr<spdlog::logger>                                      _logNotify = spdlog::default_logger()->clone("Notify");

   public:
    NotifyContext() noexcept
    {
        _logNotify->set_level(spdlog::level::trace);
    }

    void Render()
    {
        // Below code makes multithreading notification request available.
        decltype(_queue) newToasts;
        {
            std::lock_guard _lc_{_mtxQueue};
            newToasts = std::move(_queue);
        }

        // Make all pending toasts current
        while (not newToasts.empty())
        {
            auto  iter                = newToasts.begin();
            auto& ptoast              = *iter;
            ptoast->stateHeightOffset = 44;

            if (not _toasts.empty())
                ptoast->stateHeightOffset += _toasts.back()->stateHeightOffset;

            _toasts.splice(_toasts.end(), newToasts, newToasts.begin());
            ptoast->Birth = steady_clock::now();

            if (not ptoast->bInfinity)
                _timeouts.try_emplace(ptoast->Lifespan, iter);

            if (_idPool.empty())
                ptoast->stateIdAlloc = _toasts.size();
            else
                ptoast->stateIdAlloc = _idPool.back(), _idPool.pop_back();
        }

        bool bExpireAtLeastOne = false;

        // Display all toasts
        {
            using namespace ImGui;
            auto sizeVp = GetMainViewport()->Size;
            auto posVp  = GetMainViewport()->Pos;
            posVp.x += sizeVp.x;
            posVp.y += sizeVp.y;

            constexpr auto ToastFlags = ImGuiWindowFlags_AlwaysAutoResize
                                      | ImGuiWindowFlags_NoResize
                                      | ImGuiWindowFlags_NoScrollbar
                                      | ImGuiWindowFlags_NoCollapse
                                      | ImGuiWindowFlags_NoNav
                                      | ImGuiWindowFlags_NoTitleBar
                                      | ImGuiWindowFlags_NoBringToFrontOnFocus
                                      | ImGuiWindowFlags_NoFocusOnAppearing
                                      | ImGuiWindowFlags_NoSavedSettings;
            constexpr auto PaddingX        = 20.f;
            constexpr auto PaddingY        = 20.f;
            constexpr auto PaddingMessageY = 10.f;
            constexpr auto Transition      = 0.4f;
            constexpr auto DefaultOpacity  = 0.6f;

            using Seconds                  = std::chrono::duration<double>;
            float      height              = 0.f;
            auto       timeNow             = steady_clock::now();
            auto const deltaTime           = ImGui::GetIO().DeltaTime;
            auto const heightDecVal        = 80.f * deltaTime;

            for (auto iter = _toasts.begin(); iter != _toasts.end();)
            {
                auto& toast = *iter;

                {
                    auto fixedDecr           = toast->stateHeightOffset - heightDecVal;
                    auto rationalDescr       = toast->stateHeightOffset * (1.f - 6.f * deltaTime);
                    toast->stateHeightOffset = std::max(0.f, std::min(fixedDecr, rationalDescr));
                }

                auto   entityHeight = height + (*iter)->stateHeightOffset;
                ImVec2 nextPos(posVp.x - PaddingX, posVp.y - PaddingY - entityHeight);
                SetNextWindowPos(nextPos, ImGuiCond_Always, ImVec2(1.f, 1.f));

                if (entityHeight + toast->toastHeightCache > sizeVp.y)
                {
                    bExpireAtLeastOne = true;
                    ++iter;
                    break;
                }

                switch (toast->Severity)
                {
                    case NotifySeverity::Trivial: PushStyleColor(ImGuiCol_Border, 0xff'cccccc); break;
                    case NotifySeverity::Info: PushStyleColor(ImGuiCol_Border, 0xff'44ff44); break;
                    case NotifySeverity::Warning: PushStyleColor(ImGuiCol_Border, 0xff'22ffff); break;
                    case NotifySeverity::Error: PushStyleColor(ImGuiCol_Border, 0xff'6666ff); break;
                    case NotifySeverity::Fatal: PushStyleColor(ImGuiCol_Border, 0xff'0000ff); break;
                }
                CPPH_CALL_ON_EXIT(PopStyleColor());

                float timeFromSpawn    = Seconds(timeNow - toast->Birth).count();
                float timeUntilDispose = toast->bInfinity ? Transition : Seconds(toast->Lifespan - timeNow).count();

                float opacity          = DefaultOpacity * std::min(timeFromSpawn / Transition, timeUntilDispose / Transition);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, opacity);
                CPPH_CALL_ON_EXIT(ImGui::PopStyleVar());

                auto wndFlags  = ToastFlags;
                bool bKeepOpen = true;
                SetNextWindowSizeConstraints({150, -1}, sizeVp);
                Begin(perfkit::futils::usprintf("###PDASH_TOAST%d", toast->stateIdAlloc), &bKeepOpen, wndFlags);
                CPPH_CALL_ON_EXIT(End());

                PushTextWrapPos(sizeVp.x / 4.f);
                CPPH_CALL_ON_EXIT(PopTextWrapPos());

                // Close condition
                bool bCloseToast = not bKeepOpen;
                if (IsWindowHovered() && IsMouseDoubleClicked(0))
                    bCloseToast = true;

                // Render all decorations
                for (auto& deco : toast->ContentDecos)
                {
                    bCloseToast |= deco();
                }

                // If given toast is erased ...
                if (bCloseToast)
                {
                    preEraseToast(iter);

                    auto [it, end] = _timeouts.equal_range(toast->Lifespan);
                    if (it != end)
                    {
                        for (; it->second != iter; ++it) {}
                        _timeouts.erase(it);
                    }

                    iter = _toasts.erase(iter);
                    continue;
                }

                height += (toast->toastHeightCache = GetWindowHeight() + PaddingMessageY);
                ++iter;
            }
        }

        if (bExpireAtLeastOne && not _timeouts.empty())
        {
            auto it = _timeouts.begin();
            for (auto iter = it->second; ++iter != _toasts.end();)
                (**iter).stateHeightOffset -= (**it->second).toastHeightCache;

            preEraseToast(it->second);
            _toasts.erase(it->second);
            _timeouts.erase(it);
        }

        // Erase expired toasts
        {
            auto now = steady_clock::now();
            auto end = _timeouts.upper_bound(now);

            for (auto& [_, iter] : perfkit::make_iterable(_timeouts.begin(), end))
            {
                preEraseToast(iter);
                _toasts.erase(iter);
            }

            _timeouts.erase(_timeouts.begin(), end);
        }
    }

    void Commit(NotifyToast&& toast)
    {
        std::lock_guard _lc_{_mtxQueue};
        _queue.emplace_back(std::move(toast._body));
    }

    void Logging(spdlog::level::level_enum loglevel, string const& content)
    {
        _logNotify->log(loglevel, content);
    }

   private:
    void preEraseToast(decltype(_toasts)::iterator iter)
    {
        ImGui::SetWindowSize(usprintf("###PDASH_TOAST%d", (*iter)->stateIdAlloc), {1, 1}, ImGuiCond_Always);
        _idPool.push_back((*iter)->stateIdAlloc);

        auto offset = (*iter)->toastHeightCache;
        while (++iter != _toasts.end())
            (*iter)->stateHeightOffset += offset;
    }
} gNoti;

static spdlog::level::level_enum
toSpdlogLevel(NotifySeverity value)
{
    switch (value)
    {
        case NotifySeverity::Trivial:
            return spdlog::level::debug;

        case NotifySeverity::Info:
            return spdlog::level::info;

        case NotifySeverity::Warning:
            return spdlog::level::warn;

        case NotifySeverity::Error:
            return spdlog::level::err;

        case NotifySeverity::Fatal:
            return spdlog::level::critical;

        default:
            return spdlog::level::trace;
    }
}

NotifyToast&& NotifyToast::String(string content) &&
{
    gNoti.Logging(toSpdlogLevel(_body->Severity), content);

    _body->ContentDecos.emplace_back(
            [content = std::move(content)] {
                ImGui::TextUnformatted(content.c_str(), content.c_str() + content.size());
                return false;
            });

    return _self();
}

NotifyToast&& NotifyToast::Button(function<void()> handler, string label) &&
{
    _body->ContentDecos.emplace_back(
            [handler = std::move(handler), label = std::move(label)] {
                if (ImGui::Button(label.c_str()))
                {
                    handler();
                    return true;
                }
                else
                {
                    return false;
                }
            });

    return _self();
}

NotifyToast::~NotifyToast()
{
    if (not _body) { return; }
    gNoti.Commit(std::move(*this));
}

NotifyToast&& NotifyToast::Custom(function<bool()> handler) &&
{
    _body->ContentDecos.emplace_back(std::move(handler));
    return _self();
}

NotifyToast&& NotifyToast::Title(string content) &&
{
    gNoti.Logging(toSpdlogLevel(_body->Severity), "<Title> " + content);

    _body->ContentDecos.emplace_back(
            [severity = &_body->Severity, content = std::move(content)] {
                using namespace ImGui;

                switch (*severity)
                {
                    case NotifySeverity::Trivial: PushStyleColor(ImGuiCol_Text, 0xff'cccccc); break;
                    case NotifySeverity::Info: PushStyleColor(ImGuiCol_Text, 0xff'44dd44); break;
                    case NotifySeverity::Warning: PushStyleColor(ImGuiCol_Text, 0xff'22ffff); break;
                    case NotifySeverity::Error: PushStyleColor(ImGuiCol_Text, 0xff'6666ff); break;
                    case NotifySeverity::Fatal: PushStyleColor(ImGuiCol_Text, 0xff'0000ff); break;
                }

                ImGui::AlignTextToFramePadding(),
                        ImGui::TextUnformatted(content.c_str());
                ImGui::Separator();

                ImGui::PopStyleColor();
                return false;
            });

    return _self();
}

NotifyToast&& NotifyToast::Separate() &&
{
    _body->ContentDecos.emplace_back(
            [] {
                ImGui::Separator();
                return false;
            });

    return _self();
}

NotifyToast&& NotifyToast::Spinner(int color) &&
{
    _body->ContentDecos.emplace_back(
            [color] {
                ImGui::Spinner("SpinnerCommon", color);
                ImGui::SameLine();
                return false;
            });

    return _self();
}

void RenderNotifies()
{
    gNoti.Render();
}
