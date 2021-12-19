//
// Created by ki608 on 2021-12-19.
//

#include "session_slot_trace_context.hpp"

#include <imgui-extension.h>
#include <range/v3/view/transform.hpp>

#include "perfkit/common/algorithm.hxx"

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
        if (not ImGui::CollapsingHeader(_label("{}", trace.class_name)))
            continue;

        ImGui::TreePush();

        ImGui::TreePop();
    }
}
