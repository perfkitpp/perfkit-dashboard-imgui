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

#include "Application.hpp"
#include "imgui.h"

static class NotifyContext
{
    std::list<NotifyToast::Content> _queue;
    std::mutex _mtxQueue;

    std::list<NotifyToast::Content> _toasts;
    std::map<steady_clock::time_point, decltype(_toasts)::iterator> _timeouts;

    vector<int> _idPool;

   public:
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
            auto iter = newToasts.begin();
            _toasts.splice(_toasts.end(), newToasts, newToasts.begin());
            _timeouts.try_emplace(iter->Lifespan, iter);
            iter->Birth = steady_clock::now();

            if (_idPool.empty())
                iter->IdAllocated = _toasts.size();
            else
                iter->IdAllocated = _idPool.back(), _idPool.pop_back();
        }

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
                                      | ImGuiWindowFlags_NoBringToFrontOnFocus
                                      | ImGuiWindowFlags_NoFocusOnAppearing;
            constexpr auto PaddingX        = 20.f;
            constexpr auto PaddingY        = 20.f;
            constexpr auto PaddingMessageY = 10.f;
            constexpr auto Transition      = 0.4f;
            constexpr auto DefaultOpacity  = 0.6f;

            float height   = 0.;
            auto timeNow   = steady_clock::now();
            using secondsf = std::chrono::duration<double>;

            for (auto iter = _toasts.begin(); iter != _toasts.end();)
            {
                auto& toast = *iter;

                SetNextWindowPos(
                        ImVec2(posVp.x - PaddingX, posVp.y - PaddingY - height),
                        ImGuiCond_Always,
                        ImVec2(1.f, 1.f));

                switch (toast.Severity)
                {
                    case NotifySeverity::Trivial: PushStyleColor(ImGuiCol_Border, 0xff'cccccc); break;
                    case NotifySeverity::Info: PushStyleColor(ImGuiCol_Border, 0xff'44ff44); break;
                    case NotifySeverity::Warning: PushStyleColor(ImGuiCol_Border, 0xff'22ffff); break;
                    case NotifySeverity::Error: PushStyleColor(ImGuiCol_Border, 0xff'6666ff); break;
                    case NotifySeverity::Fatal: PushStyleColor(ImGuiCol_Border, 0xff'0000ff); break;
                }
                CPPH_CALL_ON_EXIT(PopStyleColor());

                float timeFromSpawn    = secondsf(timeNow - toast.Birth).count();
                float timeUntilDispose = secondsf(toast.Lifespan - timeNow).count();

                float opacity = DefaultOpacity * std::min(timeFromSpawn / Transition, timeUntilDispose / Transition);
                SetNextWindowBgAlpha(opacity);

                auto flags = ToastFlags;
                if (toast.Title.empty()) { flags |= ImGuiWindowFlags_NoTitleBar; }

                bool bKeepOpen = true;
                Begin(perfkit::futils::usprintf("%s###PDASH_TOAST%d", toast.Title.c_str(), toast.IdAllocated), &bKeepOpen, flags);
                CPPH_CALL_ON_EXIT(End());

                PushTextWrapPos(sizeVp.x / 4.f);
                CPPH_CALL_ON_EXIT(PopTextWrapPos());

                // Close condition
                bool bCloseToast = not bKeepOpen;
                if (IsWindowHovered() && IsMouseClicked(ImGuiMouseButton_Middle))
                    bCloseToast = true;

                // Render all decorations
                for (auto& deco : toast.ContentDecos)
                {
                    bCloseToast |= deco();
                }

                // If given toast is erased ...
                if (bCloseToast)
                {
                    auto [it, end] = _timeouts.equal_range(toast.Lifespan);
                    assert(it != end);

                    for (; it->second != iter; ++it) {}
                    assert(it->second == iter);

                    _timeouts.erase(it);
                    _idPool.push_back(toast.IdAllocated);
                    iter = _toasts.erase(iter);
                    continue;
                }

                height += GetWindowHeight() + PaddingMessageY;
                ++iter;
            }
        }

        // Erase expired toasts
        {
            auto now = steady_clock::now();
            auto end = _timeouts.upper_bound(now);

            for (auto& [_, iter] : perfkit::make_iterable(_timeouts.begin(), end))
            {
                _idPool.push_back(iter->IdAllocated);
                _toasts.erase(iter);
            }

            _timeouts.erase(_timeouts.begin(), end);
        }
    }

    void Commit(NotifyToast&& toast)
    {
        std::lock_guard _lc_{_mtxQueue};
        _queue.emplace_back(std::move(*toast._body));
        toast._body.reset();
    }
} gNoti;

NotifyToast&& NotifyToast::AddString(string content) &&
{
    _body->ContentDecos.emplace_back(
            [content = std::move(content)] {
                ImGui::TextUnformatted(content.c_str(), content.c_str() + content.size());
                return false;
            });

    return _self();
}

NotifyToast&& NotifyToast::AddButton(function<void()> handler, string label) &&
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

void NotifyToast::Commit() &&
{
    gNoti.Commit(std::move(*this));
}

void RenderNotifies()
{
    gNoti.Render();
}
