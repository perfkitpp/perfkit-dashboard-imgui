// MIT License
//
// Copyright (c) 2022. Seungwoo Kang
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

#pragma once
#include <string>
#include <unordered_set>

#include <spdlog/spdlog.h>

#include "classes/session_context.hpp"
#include "perfkit/common/circular_queue.hxx"
#include "perfkit/common/format.hxx"
#include "perfkit/common/timer.hxx"
#include "perfkit/common/utility/ownership.hxx"

using namespace std::literals;

class session_slot_trace_context
{
   public:
    using node_type = session_context::trace_result_type::node_scheme;

   private:
    struct trace_class_context
    {
        std::string class_name;
        uint64_t instance_id = 0;

        bool tracing        = 0;
        size_t update_index = 0;
        perfkit::poll_timer tim_next_signal{50ms};
        perfkit::stopwatch tim_last_request;
        std::future<session_context::trace_result_type> fut_result;
        perfkit::ownership<session_context::trace_result_type> result;

        std::unordered_set<uint64_t> relates;
    };

    struct plot_arg
    {
        perfkit::stopwatch timestamp;  // x axis
        double value;                  // y axis
    };

    struct node_context
    {
        std::string display_key;
        bool plotting = false;

        perfkit::stopwatch tim_plot_begin;
        perfkit::circular_queue<plot_arg> graph{0};

        // caches
        bool plot_dirty = false;
        std::vector<double> plot_x;
        std::vector<double> plot_y;
        size_t plot_cursor = 0;
        int plot_axis_n    = 0;
        uint32_t color     = 0xffffffff;

        enum plot_style
        {
            // TODO: DO THIS!
            LINE_STYLE_LINE,
            LINE_STYLE_SHADED,
            LINE_STYLE_STEM,
        } style;

        double plot_pivot_if_required = 0.;
    };

   public:
    session_slot_trace_context(std::string url, session_context* context)
            : _url(std::move(url)),
              _context(context) {}

    ~session_slot_trace_context()
    {
        for (auto& [_, trace] : _traces)
        {
            if (trace.fut_result.valid())
            {
                try
                {
                    trace.fut_result.get();
                }
                catch (std::future_error& e)
                {
                    SPDLOG_INFO("couldn't receive trace result \"{}\"", trace.class_name);
                }
            }
        }
    }

    // 활성화 시 업데이트 로직.
    void update_selected();
    void update_always() {}

   private:
    void _check_new_classes();
    void _fetch_update_traces();
    void _plot_window();

    void _cleanup_context(trace_class_context* context)
    {
        // TODO: 모든 연관된 트레이스 노드 제거 (relates)
        for (auto key : context->relates)
        {
            _nodes.erase(key);
        }

        try
        {
            *context = {};
        }
        catch (std::future_error& ec)
        {
            SPDLOG_ERROR("future error: {}", ec.what());
        }
    }

   private:
    template <typename Str_, typename... Args_>
    char const* _label(Str_&& fmt, Args_&&... args)
    {
        _fmt_label.clear();
        _fmt_label.format_append(std::forward<Str_>(fmt), std::forward<Args_>(args)...);
        _fmt_label.format_append("{}", _url);
        return _fmt_label.c_str();
    }

    void _recursive_draw_trace(node_type const* node);

   private:
    std::string const _url;
    session_context* const _context;
    std::map<std::string, trace_class_context, std::less<>> _traces;

    perfkit::format_buffer _fmt_label, _tmp;
    std::unordered_map<uint64_t, node_context> _nodes;
    std::vector<node_type const*> _node_stack;

    bool _plotting_any = false;

    std::string_view _cur_class;
    uint64_t _cur_class_id = 0;
    bool _cur_has_update   = false;
};
