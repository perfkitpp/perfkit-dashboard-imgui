//
// Created by ki608 on 2021-12-19.
//

#include "session_slot_trace_context.hpp"

#include <charconv>

#include <imgui-extension.h>
#include <implot.h>
#include <range/v3/view.hpp>

#include "imgui_internal.h"
#include "perfkit/common/algorithm.hxx"

namespace views = ranges::views;

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
    _plot_window();
}

enum
{
    PLOT_BUFFER_SIZE = 1024
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
            _tmp.format("{}@", _cur_class);
            for (auto hkey : _node_stack)
                _tmp.format_append("{}/", hkey->name);
            _tmp.pop_back();

            ctx->display_key = _tmp;

            static int order = 0;
            auto hash        = perfkit::hasher::fnv1a_64(++order);
            uint8_t color[4] = {0, 0, 0, 0xff};

            color[0]   = (hash >>= 8) & 0xff | 0x10;
            color[1]   = (hash >>= 8) & 0xff | 0x10;
            color[2]   = (hash >>= 8) & 0xff | 0x10;
            ctx->color = *(uint32_t*)color;
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

void session_slot_trace_context::_fetch_update_traces()
{
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
            max           = 1000.;
            ImGui::SliderScalar(
                    _label("Update Interval##{}", trace.class_name),
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
        _cur_class = trace.class_name;
        _node_stack.push_back(&trace.result->root);
        _recursive_draw_trace(&trace.result->root);
        _node_stack.pop_back();

        _cur_class      = {};
        _cur_has_update = false;
        assert_(_node_stack.empty());
    }
}

void session_slot_trace_context::_plot_window()
{
    if (not ImGui::Begin("Trace Plots", nullptr))
    {
        ImGui::End();
        return;
    }

    void* selected_window = nullptr;
    static std::vector<node_context*> draw_targets;

    if (selected_window != this)
    {
        selected_window = this;
        draw_targets.clear();
    }

    // TODO: Plotting Window
    //       - 모든 트레이스 클래스에서 plotting 활성화된 노드 리스트
    //       - 다수의 노드 선택 시 중첩 그리기
    //       - 각 노드의 그리기 모드 지정
    //       - 두 개의 서로 다른 노드 x, y 병합 (타임스탬프로)

    static ImVec2 window_size;
    static uint64_t color_editing_target = 0;
    auto constexpr DRAG_DROP_CLASS       = "TracePlotArg";

    if (decltype(&*_nodes.end()) edit_target = nullptr;
        color_editing_target != 0
        && nullptr != (edit_target = perfkit::find_ptr(_nodes, color_editing_target)))
    {
        ImGui::SetNextWindowSize({320, 400});
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

    ImGui::BeginChild("TracePlotDndLeft", {200, -1}, true, ImGuiWindowFlags_HorizontalScrollbar);

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

    for (auto& [key, ctx] : _nodes)
    {  // plot 리스트 렌더
        if (not ctx.plotting)
            continue;

        if (ImPlot::ItemIcon(ctx.color), ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {  // 색상 편집
            color_editing_target = key;
        }

        ImGui::SameLine(), ImGui::Selectable(ctx.display_key.c_str());

        if (ImGui::BeginDragDropSource())
        {  // Drag & Drop
            ImGui::SetDragDropPayload(DRAG_DROP_CLASS, &key, sizeof key);
            ImPlot::ItemIcon(ctx.color), ImGui::SameLine();
            ImGui::TextUnformatted(ctx.display_key.c_str());
            ImGui::EndDragDropSource();
        }
    }
    ImGui::EndChild();

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

    ImGui::SameLine();
    ImGui::BeginChild("TracePlotDndRight", {-1, -1}, false);

    static double axis_limits[2];
    axis_limits[0] = std::min(0., axis_limits[0]);
    ImPlot::SetNextAxisLinks(ImAxis_X1, axis_limits + 0, axis_limits + 1);
    if (ImPlot::BeginPlot("##TracePlotDnd", {-1, -1}))
    {
        ImPlot::SetupAxisLimits(ImAxis_X1, -1., 0.);
        ImPlot::SetupAxis(ImAxis_X1, NULL, ImPlotAxisFlags_RangeFit);

        ImPlot::SetupAxis(ImAxis_Y1, "[drop here]");
        ImPlot::SetupAxis(ImAxis_Y2, "[drop here]", ImPlotAxisFlags_Opposite);
        ImPlot::SetupAxis(ImAxis_Y3, "[drop here]", ImPlotAxisFlags_Opposite);

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

        for (auto& [key, ctx] : _nodes)
        {
            if (not ctx.plotting || ctx.plot_axis_n == 0)
                continue;

            if (ctx.plot_dirty)
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
            ImPlot::SetNextLineStyle(ImGui::ColorConvertU32ToFloat4(ctx.color));
            ImPlot::PlotLine(
                    ctx.display_key.c_str(),
                    ctx.plot_x.data(),
                    ctx.plot_y.data(),
                    ctx.plot_x.size());

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

    ImGui::EndChild();
    ImGui::End();
}
