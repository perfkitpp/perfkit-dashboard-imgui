//
// Created by ki608 on 2021-12-19.
//

#include "session_slot_trace_context.hpp"

#include <charconv>

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

size_t count_traces(session_slot_trace_context::node_type const* n)
{
    size_t num = n->children.size();
    for (auto& child : n->children)
    {
        num += count_traces(&child);
    }
    return num;
}

void session_slot_trace_context::update_selected()
{
    if (auto class_update = _context->check_trace_class_change())
    {
        std::vector<std::string_view> diffs;
        diffs.reserve(class_update->size());

        if (_traces.empty())
        {
            diffs.assign(class_update->begin(), class_update->end());
        }
        else
        {
            perfkit::set_difference2(
                    *class_update,
                    _traces | ranges::views::transform([](auto&& s) { return s.class_name; }),
                    std::back_inserter(diffs),
                    [](auto&& a, auto&& b) {
                        return a < b;
                    });
        }

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
                    _label("{}##", trace.class_name),
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
                    bool is_first = not trace.result;

                    trace.result = trace.fut_result.get();
                    ++trace.update_index;

                    if (is_first)
                    {
                        _nodes.reserve(_nodes.size() + count_traces(&trace.result->root));
                    }
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
                        _label("loading symbol##"),
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
            continue;
        }

        // render trace result
        _cur_class = trace.class_name;
        _recursive_draw_trace(&trace.result->root);
    }
}

void session_slot_trace_context::_recursive_draw_trace(
        const session_slot_trace_context::node_type* node)
{
    using namespace perfkit::terminal::net::outgoing;
    char const *begin = node->value.data(), *end = node->value.data() + node->value.size();

    auto [it_node, is_new_node] = _nodes.try_emplace(node->trace_key);
    auto ctx                    = &it_node->second;
    if (is_new_node)
    {
        ;
    }

    bool show_plot_button = false;
    bool is_fold          = node->folded;

    std::string_view text;
    ImGuiCol text_color = 0xffcccccc;

    switch (trace_value_type(node->value_type))
    {
        case TRACE_VALUE_NULLPTR:
            text = "[null]";
            break;

        case TRACE_VALUE_DURATION_USEC:
        {
            int64_t value;
            std::from_chars(begin, end, value);

            text = _tmp.format("{}.{} ms", value / 1000, value % 1000);
            break;
        }

        case TRACE_VALUE_INTEGER:
        case TRACE_VALUE_FLOATING_POINT:
            text             = node->value;
            show_plot_button = true;
            break;

        case TRACE_VALUE_STRING:
        case TRACE_VALUE_BOOLEAN:
            text = node->value;
            break;

        default:
            return;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, node->subscribing ? 0xff00ff00 : 0xffcccccc);
    auto tree_open    = ImGui::TreeNodeEx(_label("{}##{}.", node->name, node->trace_key));
    auto fold_toggled = ImGui::IsItemToggledOpen();
    ImGui::PopStyleColor();

    // -- subscribe button
    ImGui::SameLine();
    ImGui::SmallButton(node->subscribing ? "*" : "+");

    // -- plot tracking button
    ImGui::SameLine();
    ImGui::SmallButton("%");

    // -- value label
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);
    ImGui::SameLine(0, 5.f);
    ImGui::TextEx(text.data(), text.data() + text.size());
    ImGui::PopStyleColor();

    if (fold_toggled)
    {  // Trace control request
        auto fold = not tree_open;
        _context->control_trace(_cur_class, node->trace_key, nullptr, &fold);
    }

    if (not tree_open)
    {  // stop recursion
        return;
    }

    // -- child nodes
    for (auto& n : node->children)
    {
        _recursive_draw_trace(&n);
    }

    ImGui::TreePop();
}
