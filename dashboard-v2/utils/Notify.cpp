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
            auto iter               = newToasts.begin();
            iter->stateHeightOffset = 44;

            if (not _toasts.empty())
                iter->stateHeightOffset += _toasts.back().stateHeightOffset;

            _toasts.splice(_toasts.end(), newToasts, newToasts.begin());
            iter->Birth = steady_clock::now();

            if (not iter->bInfinity)
                _timeouts.try_emplace(iter->Lifespan, iter);

            if (_idPool.empty())
                iter->stateIdAlloc = _toasts.size();
            else
                iter->stateIdAlloc = _idPool.back(), _idPool.pop_back();
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
                                      | ImGuiWindowFlags_NoTitleBar
                                      | ImGuiWindowFlags_NoBringToFrontOnFocus
                                      | ImGuiWindowFlags_NoFocusOnAppearing;
            constexpr auto PaddingX        = 20.f;
            constexpr auto PaddingY        = 20.f;
            constexpr auto PaddingMessageY = 10.f;
            constexpr auto Transition      = 0.4f;
            constexpr auto DefaultOpacity  = 0.6f;

            float height            = 0.f;
            auto timeNow            = steady_clock::now();
            using secondsf          = std::chrono::duration<double>;
            auto const heightDecVal = 360.f * ImGui::GetIO().DeltaTime;

            for (auto iter = _toasts.begin(); iter != _toasts.end();)
            {
                auto& toast             = *iter;
                toast.stateHeightOffset = std::max(0.f, toast.stateHeightOffset - heightDecVal);

                SetNextWindowPos(
                        ImVec2(posVp.x - PaddingX, posVp.y - PaddingY - height - iter->stateHeightOffset),
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
                float timeUntilDispose = toast.bInfinity ? Transition : secondsf(toast.Lifespan - timeNow).count();

                float opacity = DefaultOpacity * std::min(timeFromSpawn / Transition, timeUntilDispose / Transition);
                SetNextWindowBgAlpha(opacity);

                auto wndFlags  = ToastFlags;
                bool bKeepOpen = true;
                SetNextWindowSizeConstraints({150, -1}, sizeVp);
                Begin(perfkit::futils::usprintf("###PDASH_TOAST%d", toast.stateIdAlloc), &bKeepOpen, wndFlags);
                CPPH_CALL_ON_EXIT(End());

                PushTextWrapPos(sizeVp.x / 4.f);
                CPPH_CALL_ON_EXIT(PopTextWrapPos());

                // Close condition
                bool bCloseToast = not bKeepOpen;
                if (IsWindowHovered() && IsMouseDoubleClicked(0))
                    bCloseToast = true;

                // Render all decorations
                for (auto& deco : toast.ContentDecos)
                {
                    bCloseToast |= deco();
                }

                // If given toast is erased ...
                if (bCloseToast)
                {
                    preEraseToast(iter);

                    auto [it, end] = _timeouts.equal_range(toast.Lifespan);
                    if (it != end)
                    {
                        for (; it->second != iter; ++it) {}
                        _timeouts.erase(it);
                    }

                    iter = _toasts.erase(iter);
                    continue;
                }

                height += (toast.toastHeightCache = GetWindowHeight() + PaddingMessageY);
                ++iter;
            }
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
        _queue.emplace_back(std::move(*toast._body));
        toast._body.reset();
    }

   private:
    void preEraseToast(decltype(_toasts)::iterator iter)
    {
        _idPool.push_back(iter->stateIdAlloc);

        auto offset = iter->toastHeightCache;
        while (++iter != _toasts.end())
            iter->stateHeightOffset += offset;
    }
} gNoti;

NotifyToast&& NotifyToast::String(string content) &&
{
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

void NotifyToast::Commit() &&
{
    gNoti.Commit(std::move(*this));
}

NotifyToast&& NotifyToast::Custom(function<bool()> handler) &&
{
    _body->ContentDecos.emplace_back(std::move(handler));
    return _self();
}

NotifyToast&& NotifyToast::Title(string content) &&
{
    _body->ContentDecos.emplace_back(
            [severity = _body->Severity, content = std::move(content)] {
                using namespace ImGui;

                switch (severity)
                {
                    case NotifySeverity::Trivial: PushStyleColor(ImGuiCol_Text, 0xff'cccccc); break;
                    case NotifySeverity::Info: PushStyleColor(ImGuiCol_Text, 0xff'44ff44); break;
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

void RenderNotifies()
{
    gNoti.Render();
}
