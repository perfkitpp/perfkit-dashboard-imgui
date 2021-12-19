//
// Created by ki608 on 2021-12-19.
//

#include "session_slot_trace_context.hpp"

#include <imgui-extension.h>
#include <range/v3/view/transform.hpp>

#include "imgui_internal.h"
#include "perfkit/common/algorithm.hxx"

static void push_button_color_series(ImGuiCol index, int32_t code)
{
    ImGui::PushStyleColor(index, code);
    ImGui::PushStyleColor(index + 1, code + 0x111111);
    ImGui::PushStyleColor(index + 2, code + 0x222222);
}

static void pop_button_color_series()
{
    ImGui::PopStyleColor(3);
}

void session_slot_trace_context::update_selected()
{
    if (auto class_update = _context->check_trace_class_change())
    {
        std::vector<std::string_view> diffs;
        diffs.reserve(class_update->size());

        perfkit::set_difference2(
                *class_update,
                _traces | ranges::views::transform([](auto&& s) { return s.class_name; }),
                std::back_inserter(diffs),
                [](auto&& a, auto&& b) {
                    return a < b;
                });

        if (not diffs.empty())
        {
            SPDLOG_INFO("{}:{} trace classes newly recognized.", _url, diffs.size());

            for (auto class_name : diffs)
            {
                _traces.emplace_back().class_name = class_name;
            }
        }
    }

    for (auto& trace : _traces)
    {
        if (trace.tracing)
            push_button_color_series(ImGuiCol_Header, 0xff407857);
        else
            push_button_color_series(ImGuiCol_Header, 0xff3d3d3d);

        {  // draw trace class label
            auto spin = ":._-^'\"~\\|/*"sv;

            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, trace.tracing ? 0xffaaffff : 0xff0000ff);
            ImGui::Text("%c", trace.tracing ? spin[trace.update_index % spin.size()] : '*');
            ImGui::PopStyleColor();

            ImGui::AlignTextToFramePadding();
            ImGui::SameLine();
            ImGui::Selectable(
                    _label("{}", trace.class_name),
                    &trace.tracing,
                    ImGuiSelectableFlags_SpanAvailWidth);
        }

        pop_button_color_series();

        if (not trace.tracing)
            continue;

        if (trace.fut_result.valid())
        {
            auto r_wait = trace.fut_result.wait_for(0ms);

            if (r_wait == std::future_status::ready)
            {
                try
                {
                    trace.result = trace.fut_result.get();
                    ++trace.update_index;
                }
                catch (std::future_error& e)
                {
                    SPDLOG_ERROR(
                            "failed to retrive trace result somewhat mysterious error {}",
                            e.what());
                }
            }
            else if (r_wait == std::future_status::timeout
                     && trace.tim_last_request.elapsed() > 0.25s)
            {
                ImGui::SameLine(ImGui::GetWindowWidth() - 25.f);
                ImGui::Spinner(
                        _label("loading symbol"),
                        0xffffffff,
                        5.,
                        2, trace.tim_last_request.elapsed().count());
            }
        }

        auto too_old = trace.tim_last_request.elapsed() > 10s;  // 무응답 시 타임아웃 처리
        if ((too_old || not trace.fut_result.valid()) && trace.tim_next_signal.check())
        {
            trace.fut_result = _context->signal_fetch_trace(trace.class_name);
            trace.tim_last_request.reset();
        }

        if (not trace.result)
        {
            ImGui::Text("-- fetching --");
        }
    }
}
