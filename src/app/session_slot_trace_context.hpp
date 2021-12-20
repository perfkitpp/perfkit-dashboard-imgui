//
// Created by ki608 on 2021-12-19.
//

#pragma once
#include <string>

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
        bool tracing        = 0;
        size_t update_index = 0;
        perfkit::poll_timer tim_next_signal{50ms};
        perfkit::stopwatch tim_last_request;
        std::future<session_context::trace_result_type> fut_result;
        perfkit::ownership<session_context::trace_result_type> result;
    };

    struct plot_arg
    {
        double time_offset;  // x axis
        double value;        // y axis
    };

    struct node_context
    {
        bool plotting = false;
        perfkit::stopwatch tim_plot_begin;
        perfkit::circular_queue<plot_arg> graph{256};
    };

   public:
    session_slot_trace_context(std::string url, session_context* context)
            : _url(std::move(url)),
              _context(context) {}

    ~session_slot_trace_context()
    {
        for (auto& trace : _traces)
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
    std::list<trace_class_context> _traces;

    perfkit::format_buffer _fmt_label, _tmp;
    std::unordered_map<uint64_t, node_context> _nodes;

    std::string_view _cur_class;
    bool _cur_has_update = false;
};
