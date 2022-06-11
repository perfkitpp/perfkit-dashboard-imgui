// MIT License
//
// Copyright (c) 2021-2022. Seungwoo Kang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// project home: https://github.com/perfkitpp

//
// Created by ki608 on 2021-12-19.
//

#include "session_slot_trace_context.hpp"

#include <charconv>
#include <set>

#include <imgui_extension.h>
#include <implot.h>
#include <range/v3/view/map.hpp>
#include <range/v3/view/transform.hpp>

#include "imgui_internal.h"
#include "cpphalgorithm/std.hxx"
#include "cpph/counter.hxx"
#include "cpph/macros.hxx"
#include "cpph/utility/cleanup.hxx"
#include "cpph/zip.hxx"

namespace views = ranges::views;
using namespace perfkit::utilities;

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
    _check_new_classes();
    _fetch_update_traces();

    if (_plotting_any) { _plot_window(); }
}

enum
{
    PLOT_BUFFER_SIZE = 2000
};

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
            // Generate display key name
            _tmp.format("{}@", _cur_class);
            for (auto hkey : _node_stack)
                _tmp.format_append("{}/", hkey->name);
            _tmp.pop_back();

            ctx->display_key = _tmp;

            // Generate plot color randomly
            static int order = 0;
            auto hash        = perfkit::hasher::fnv1a_64(++order);
            uint8_t color[4] = {0, 0, 0, 0xff};

            color[0]   = (hash >>= 8) & 0xff | 0x10;
            color[1]   = (hash >>= 8) & 0xff | 0x10;
            color[2]   = (hash >>= 8) & 0xff | 0x10;
            ctx->color = *(uint32_t*)color;

            _traces.find(_cur_class)->second.relates.insert(node->trace_key);

            // Validate fold status
            ImGui::SetNextItemOpen(not node->folded);
        }

        bool show_plot_button = false;

        std::string_view text;
        ImGuiCol text_color = 0xffcccccc;

        bool is_string = false;
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

                text_color = 0xff5cb565;
                break;

            case TRACE_VALUE_STRING:
                text       = node->value;
                text_color = 0xff2980cc;
                is_string  = true;
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
        auto tree_open = ImGui::TreeNodeEx(
                _label("{}##{}.", node->name, node->trace_key),
                ImGuiTreeNodeFlags_AllowItemOverlap
                        | ImGuiTreeNodeFlags_SpanAvailWidth
                        | ImGuiTreeNodeFlags_DefaultOpen
                        | (not node->folded && node->children.empty() ? ImGuiTreeNodeFlags_Leaf : 0));
        auto should_popup     = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
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
            auto plot_button_color = (ctx->plotting ? 0xdd34d101 : 0xdd00a7d1) + fresh_offset;
            push_button_color_series(ImGuiCol_Button, plot_button_color);
            ImGui::SameLine(0);
            if (ImGui::SmallButton(_label("%##{}.", node->trace_key)))
            {
                ctx->plotting = not ctx->plotting;
                if (ctx->plotting)
                {
                    ctx->tim_plot_begin.reset();
                    ctx->graph.clear();

                    if (ctx->plot_axis_n == 0)
                        ctx->plot_axis_n = ImAxis_Y1;
                }
            }
            pop_button_color_series();
        }

        // -- value label
        {
            ImGui::SameLine();

            auto offset = ImGui::GetWindowWidth() - text_extent.x - 35.f;
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
            _plotting_any = true;

            if (ctx->graph.capacity() == 0)
            {
                ctx->graph.reserve_shrink(PLOT_BUFFER_SIZE);
                ctx->plot_x.reserve(PLOT_BUFFER_SIZE);
                ctx->plot_y.reserve(PLOT_BUFFER_SIZE);
                ctx->plot_cursor = 0;
            }

            // 1. collect value
            if (_cur_has_update)
            {
                double plot_value = 0.;
                std::from_chars(begin, end, plot_value);

                if (node->value_type == TRACE_VALUE_DURATION_USEC)
                    plot_value *= 1e-3;

                auto arg = &ctx->graph.emplace_back();
                arg->timestamp.reset();
                arg->value = plot_value;

                ctx->plot_dirty = true;
            }

            // 2. draw if popped up
            if (should_popup)
            {
                ImGui::SetNextWindowSize({480, 320});
                ImGui::BeginTooltip();

                ImPlot::SetNextAxesToFit();
                ImPlot::SetNextAxesLimits(0, 1, 0, 1);  // reset axis limits if cached
                if (ImPlot::BeginPlot(ctx->display_key.c_str()))
                {
                    using iterator = decltype(ctx->graph.begin());

                    auto constexpr data_getter
                            = ([](void* data, int i) -> ImPlotPoint {
                                  auto iter  = (iterator*)data;
                                  auto& pair = (*iter)[i];
                                  return ImPlotPoint{-pair.timestamp.elapsed().count(), pair.value};
                              });

                    auto it = ctx->graph.begin();
                    ImPlot::SetupAxes("Timestamp", "Value");

                    ImPlot::PushStyleColor(ImPlotCol_Line, text_color);
                    ImPlot::PlotLineG("History", data_getter, &it, ctx->graph.size());
                    ImPlot::PopStyleColor();

                    ImPlot::EndPlot();
                }
                ImGui::EndTooltip();
            }
        }
        else
        {
            ctx->graph.reserve_shrink(0);
            ctx->plot_x     = {};
            ctx->plot_y     = {};
            ctx->plot_dirty = false;
        }

        if (is_string && should_popup)
        {
            static ImVec2 size{480, 272};
            ImGui::SetNextWindowSize(size);
            ImGui::SetNextWindowBgAlpha(0.67);

            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(480.);

            ImGui::TextEx(node->value.c_str(),
                          node->value.c_str() + std::min<size_t>(2500, node->value.size()));
            size = ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow());

            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        if (not tree_open)
        {  // stop recursion
            return;
        }
    }

    // -- child nodes
    for (auto& n : node->children)
    {
        _node_stack.push_back(&n);
        _recursive_draw_trace(&n);
        _node_stack.pop_back();
    }

    ImGui::TreePop();
}

void session_slot_trace_context::_check_new_classes()
{
    auto class_update = _context->check_trace_class_change();
    if (not class_update)
        return;

    namespace view   = ranges::view;
    namespace action = ranges::action;

    std::set<std::string> erases;
    auto all_keys = *class_update | view::keys;
    erases.insert(all_keys.begin(), all_keys.end());

    // 현재 트레이스 목록 iterate, 생성/제거/갱신 검출
    for (auto& [name, id] : *class_update)
    {
        auto [it, is_new] = _traces.try_emplace(name);
        auto ctx          = &it->second;

        // 지워지는 대상이 아니다.
        erases.erase(name);

        if (not is_new)
        {
            if (id == ctx->instance_id) { continue; }  // not changed.

            // 기존의 트레이스가 대체됨 ...
            _cleanup_context(&it->second);
        }

        // update context
        ctx->class_name  = name;
        ctx->instance_id = id;
    }

    // 제거된 트레이스를 cleanup
    for (auto& erased : erases)
    {
        _cleanup_context(&_traces.at(erased));
        _traces.erase(erased);
    }
}

void session_slot_trace_context::_fetch_update_traces()
{
    for (auto& [_, trace] : _traces)
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
            max           = 1000.;
            ImGui::SliderScalar(
                    _label("##Update Interval {}", trace.class_name),
                    ImGuiDataType_Double,
                    &interv, &min, &max, "Interval: %lf ms"))
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
        _cur_class    = trace.class_name;
        _cur_class_id = trace.instance_id;
        _plotting_any = false;
        _node_stack.push_back(&trace.result->root);
        _recursive_draw_trace(&trace.result->root);
        _node_stack.pop_back();

        _cur_class      = {};
        _cur_class_id   = 0;
        _cur_has_update = false;

        assert(_node_stack.empty());
    }
}

void session_slot_trace_context::_plot_window()
{
    static uint64_t color_editing_target = 0;
    static int color_edit_focus_next     = 0;
    auto constexpr DRAG_DROP_CLASS       = "TracePlotArg";
    uint64_t currently_hovered_plot_ctx  = 0;

    if (CPPH_CLEANUP(&ImGui::End);
        ImGui::Begin("Trace Plot - List", nullptr, ImGuiWindowFlags_HorizontalScrollbar))
    {
        void* selected_window = nullptr;
        static std::vector<node_context*> draw_targets;

        if (selected_window != this)
        {
            selected_window = this;
            draw_targets.clear();
        }

        if (decltype(&*_nodes.end()) edit_target = nullptr;
            color_editing_target != 0
            && nullptr != (edit_target = perfkit::find_ptr(_nodes, color_editing_target)))
        {
            ImGui::SetNextWindowSize({320, 400});
            if (color_edit_focus_next > 0)
            {
                ImGui::SetNextWindowFocus();
                color_edit_focus_next = std::max(0, color_edit_focus_next - 1);
            }

            if (bool open = true; ImGui::Begin("Trace Graph Color Editor", &open))
            {
                ImVec4 color = ImGui::ColorConvertU32ToFloat4(edit_target->second.color);
                ImGui::SetNextItemWidth(-1);
                ImGui::ColorPicker4("Trace Graph Color Picker", &color.x);
                edit_target->second.color = ImGui::ColorConvertFloat4ToU32(color);

                if (not ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow))
                    open = false;

                if (not open)
                    color_editing_target = 0;
            }
            ImGui::End();
        }

        push_button_color_series(ImGuiCol_Button, 0xFF0F3BAC);
        if (ImGui::Button("Reset All", {-1, 0}))
        {
            for (auto& ctx : _nodes | views::values)
                ctx.plot_axis_n = 0;
        }
        push_button_color_series(ImGuiCol_Button, 0xFF0FAC3B);
        if (ImGui::Button("Add All", {-1, 0}))
        {
            for (auto& ctx : _nodes | views::values)
                ctx.plot_axis_n = ImAxis_Y1;
        }
        pop_button_color_series();
        pop_button_color_series();

        ImGui::Separator();

        for (auto& [key, ctx] : _nodes)
        {  // plot 리스트 렌더
            if (not ctx.plotting)
                continue;

            if (ImPlot::ItemIcon(ctx.color), ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {  // 색상 편집
                color_editing_target  = key;
                color_edit_focus_next = 10;
            }

            ImGui::SameLine(), ImGui::Selectable(ctx.display_key.c_str(), false, ImGuiSelectableFlags_SpanAvailWidth);
            auto hovering  = ImGui::IsItemHovered();
            auto rclicked  = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            auto dbl_click = ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

            if (ImGui::BeginDragDropSource())
            {  // Drag & Drop
                ImGui::SetDragDropPayload(DRAG_DROP_CLASS, &key, sizeof key);
                ImPlot::ItemIcon(ctx.color), ImGui::SameLine();
                ImGui::TextUnformatted(ctx.display_key.c_str());
                ImGui::EndDragDropSource();
            }

            if (rclicked)
                ImGui::OpenPopup(_label("##{}.PlotTypeSelection", key));

            if (ImGui::BeginPopup(_label("##{}.PlotTypeSelection", key)))
            {
                ImGui::Text("Plot Type");
                ImGui::Separator();

                auto const labels = {"Line", "Shaded", "Stems"};
                auto style        = ctx.style;

                for (auto [idx, label] : zip(count(labels.size()), labels))
                {
                    auto selected = style == *idx;
                    if (selected)
                        ImGui::TextUnformatted("*"), ImGui::SameLine();

                    if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_DontClosePopups))
                    {
                        ctx.style = static_cast<decltype(ctx.style)>(*idx);
                        break;
                    }
                }

                auto pvalue = &ctx.plot_pivot_if_required;
                auto offset = std::max(1., std::abs(*pvalue) * ImGui::GetIO().DeltaTime * 0.25);
                auto min = *pvalue - offset, max = *pvalue + offset;

                ImGui::BeginDisabled(style == 0);
                ImGui::Separator();
                ImGui::SliderScalar(
                        "Pivot Control",
                        ImGuiDataType_Double,
                        pvalue, &min, &max);

                static float units_per_sec = 3.;
                min                        = *pvalue - units_per_sec * ImGui::GetIO().DeltaTime;
                max                        = *pvalue + units_per_sec * ImGui::GetIO().DeltaTime;

                ImGui::Separator();
                ImGui::SliderScalar(
                        "Fixed Control",
                        ImGuiDataType_Double,
                        pvalue, &min, &max);

                ImGui::DragFloat(
                        "Units/s",
                        &units_per_sec,
                        units_per_sec * 0.005, 0.0001);

                ImGui::EndDisabled();

                ImGui::EndPopup();
            }

            if (hovering)
            {
                ImGui::SetNextWindowBgAlpha(0.67);
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(ctx.display_key.c_str());
                ImGui::EndTooltip();

                currently_hovered_plot_ctx = key;
            }

            if (dbl_click && ctx.plot_axis_n == 0)
                ctx.plot_axis_n = ImAxis_Y1;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (auto payload = ImGui::AcceptDragDropPayload(DRAG_DROP_CLASS))
            {
                auto idx         = *(uint64_t*)payload->Data;
                auto ctx         = &_nodes.at(idx);
                ctx->plot_axis_n = 0;
            }

            ImGui::EndDragDropTarget();
        }
    }

    if (CPPH_CLEANUP(&ImGui::End);
        ImGui::Begin("Trace Plots"))
    {
        static double axis_limits[2];
        axis_limits[0] = std::min(0., axis_limits[0]);
        ImPlot::SetNextAxisLinks(ImAxis_X1, axis_limits + 0, axis_limits + 1);
        if (ImPlot::BeginPlot("##TracePlotDnd", {-1, -1}))
        {
            ImPlot::SetupAxisLimits(ImAxis_X1, -1., 0.);
            ImPlot::SetupAxis(ImAxis_X1, "Issued Time (seconds ago)", ImPlotAxisFlags_RangeFit);

            auto flag = ImPlotAxisFlags_NoGridLines;
            ImPlot::SetupAxis(ImAxis_Y1, "[AXIS 1]", flag);
            ImPlot::SetupAxis(ImAxis_Y2, "[AXIS 2]", flag | ImPlotAxisFlags_Opposite);
            ImPlot::SetupAxis(ImAxis_Y3, "[AXIS 3]", flag | ImPlotAxisFlags_Opposite);

            if (ImPlot::BeginDragDropTargetPlot())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_DROP_CLASS))
                {
                    auto key                   = *(uint64_t*)payload->Data;
                    _nodes.at(key).plot_axis_n = ImAxis_Y1;
                }
                ImPlot::EndDragDropTarget();
            }

            for (auto i = (int)ImAxis_Y1; i <= (int)ImAxis_Y3; ++i)
            {
                if (ImPlot::BeginDragDropTargetAxis(i))
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(DRAG_DROP_CLASS))
                    {
                        auto key                   = *(uint64_t*)payload->Data;
                        _nodes.at(key).plot_axis_n = i;
                    }

                    ImPlot::EndDragDropTarget();
                }
            }

            static perfkit::poll_timer tim_force_invalidate{500ms};
            bool force_update = tim_force_invalidate.check();

            for (auto& [key, ctx] : _nodes)
            {
                if (not ctx.plotting || ctx.plot_axis_n == 0)
                    continue;

                if (force_update || ctx.plot_dirty)
                {
                    ctx.plot_dirty   = false;
                    auto x_retriever = ctx.graph
                                     | views::transform([](plot_arg& p) {
                                           return -p.timestamp.elapsed().count();
                                       });
                    auto y_retriever = ctx.graph
                                     | views::transform([](plot_arg& p) {
                                           return p.value;
                                       });

                    ctx.plot_x.assign(x_retriever.begin(), x_retriever.end());
                    ctx.plot_y.assign(y_retriever.begin(), y_retriever.end());
                }

                ImPlot::SetAxis(ctx.plot_axis_n);

                float weight = IMPLOT_AUTO;
                if (key == currently_hovered_plot_ctx)
                {
                    weight = 3;
                    ImPlot::SetNextLineStyle({0.f, 0.f, 0.f, 1.f}, 5);
                    ImPlot::PlotLine(
                            ctx.display_key.c_str(),
                            ctx.plot_x.data(),
                            ctx.plot_y.data(),
                            ctx.plot_x.size());
                }

                switch (ctx.style)
                {
                    case node_context::LINE_STYLE_LINE:
                        ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(ctx.color), weight);
                        ImPlot::PlotLine(
                                ctx.display_key.c_str(),
                                ctx.plot_x.data(),
                                ctx.plot_y.data(),
                                ctx.plot_x.size());
                        break;

                    case node_context::LINE_STYLE_SHADED:
                        ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(ctx.color), weight);
                        ImPlot::PlotHLines(
                                ctx.display_key.c_str(),
                                &ctx.plot_pivot_if_required,
                                1);

                        ImPlot::SetNextFillStyle(ImGui::ColorConvertU32ToFloat4(ctx.color), 0.25);
                        ImPlot::PlotShaded(
                                ctx.display_key.c_str(),
                                ctx.plot_x.data(),
                                ctx.plot_y.data(),
                                ctx.plot_x.size(),
                                ctx.plot_pivot_if_required);

                        ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(ctx.color), weight);
                        ImPlot::PlotLine(
                                ctx.display_key.c_str(),
                                ctx.plot_x.data(),
                                ctx.plot_y.data(),
                                ctx.plot_x.size());
                        break;

                    case node_context::LINE_STYLE_STEM:
                        ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(ctx.color), weight);
                        ImPlot::PlotHLines(
                                ctx.display_key.c_str(),
                                &ctx.plot_pivot_if_required,
                                1);

                        ImPlot::SetNextMarkerStyle(-1, -1, ImGui::ColorConvertU32ToFloat4(ctx.color));
                        ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(ctx.color - 0x7f000000), weight);
                        ImPlot::PlotStems(
                                ctx.display_key.c_str(),
                                ctx.plot_x.data(),
                                ctx.plot_y.data(),
                                ctx.plot_x.size(),
                                ctx.plot_pivot_if_required);

                        break;
                }

                if (ImPlot::BeginDragDropSourceItem(ctx.display_key.c_str()))
                {
                    ImGui::SetDragDropPayload(DRAG_DROP_CLASS, &key, sizeof key);
                    ImPlot::ItemIcon(ctx.color), ImGui::SameLine();
                    ImGui::TextUnformatted(ctx.display_key.c_str());
                    ImGui::EndDragDropSource();
                }
            }

            ImPlot::EndPlot();
        }
    }
}