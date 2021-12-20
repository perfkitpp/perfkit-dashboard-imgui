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
                    _label("{:<16}##", trace.class_name),
                    &trace.tracing,
                    trace.tracing ? ImGuiSelectableFlags_AllowItemOverlap
                                  : ImGuiSelectableFlags_SpanAvailWidth);
        }

        pop_button_color_series();

        if (not trace.tracing)
            continue;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (double interv = trace.tim_next_signal.interval_sec().count() * 1000.,
            min           = 0.,
            max           = 3000.;
            ImGui::SliderScalar(
                    _label("Update Interval##{}", trace.class_name),
                    ImGuiDataType_Double,
                    &interv, &min, &max, "Interval: %lf ms",
                    ImGuiSliderFlags_Logarithmic))
        {
            trace.tim_next_signal.reset(1.ms * interv);
        }

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
                    _cur_has_update = true;

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

        _cur_class      = {};
        _cur_has_update = false;
    }
}

void session_slot_trace_context::_recursive_draw_trace(
        const session_slot_trace_context::node_type* node)
{
    {  // discrete to reduce stack usage
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

                text             = _tmp.format("{}.{:03} ms", value / 1000, value % 1000);
                text_color       = 0xff00a5ab;
                show_plot_button = true;

                break;
            }

            case TRACE_VALUE_INTEGER:
            case TRACE_VALUE_FLOATING_POINT:
                text             = node->value;
                show_plot_button = true;
                text_color       = 0xff5cb565;
                break;

            case TRACE_VALUE_STRING:
                text       = node->value;
                text_color = 0xff2980cc;
                break;

            case TRACE_VALUE_BOOLEAN:
                text       = node->value;
                text_color = 0xffe8682c;
                break;

            default:
                return;
        }
        auto text_extent  = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        auto fresh_offset = -(not node->is_fresh) * 0x88000000;

        auto tree_label_color = node->subscribing ? 0xff00ff00 : 0xffcccccc;
        ImGui::PushStyleColor(ImGuiCol_Text, tree_label_color + fresh_offset);
        auto tree_open        = ImGui::TreeNodeEx(_label("{}##{}.", node->name, node->trace_key));
        auto fold_toggled     = ImGui::IsItemToggledOpen();
        auto toggle_subscribe = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::PopStyleColor();

        // -- subscribe button
        if (toggle_subscribe)
        {
            auto next = not node->subscribing;
            _context->control_trace(_cur_class, node->trace_key, &next, nullptr);
        }

        // -- plot tracking button
        if (show_plot_button)
        {
            auto plot_button_color = (ctx->plotting ? 0xff34d101 : 0xff00a7d1) + fresh_offset;
            push_button_color_series(ImGuiCol_Button, plot_button_color);
            ImGui::SameLine();
            if (ImGui::SmallButton(_label(" % ##{}.", node->trace_key)))
                ctx->plotting = not ctx->plotting;
            pop_button_color_series();
        }

        // -- value label
        {
            ImGui::SameLine();

            auto offset = ImGui::GetWindowWidth() - text_extent.x - 10.f;
            if (offset < ImGui::GetCursorPosX())
                offset = 0.;

            ImGui::SameLine(offset);
        }

        ImGui::PushStyleColor(ImGuiCol_Text, text_color);
        ImGui::TextEx(text.data(), text.data() + text.size());
        ImGui::PopStyleColor();

        if (fold_toggled)
        {  // Trace control request
            auto fold = not tree_open;
            _context->control_trace(_cur_class, node->trace_key, nullptr, &fold);
        }

        if (ctx->plotting)
        {
            // 1. collect value

            // 2. draw
        }

        if (not tree_open)
        {  // stop recursion
            return;
        }
    }

    // -- child nodes
    for (auto& n : node->children)
    {
        _recursive_draw_trace(&n);
    }

    ImGui::TreePop();
}
