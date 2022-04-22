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

#pragma once
#include <future>

#include "if_session_connection.hpp"
#include "messages.hpp"
#include "cpph/array_view.hxx"
#include "cpph/assert.hxx"
#include "cpph/functional.hxx"
#include "cpph/hasher.hxx"
#include "cpph/thread/locked.hxx"

struct session_context_message_marshal_error : std::exception
{
};

namespace messages = perfkit::terminal::net;

class session_context
{
   public:
    using info_type          = perfkit::terminal::net::outgoing::session_reset;
    using config_type        = messages::outgoing::new_config_class::category_scheme;
    using config_entity_type = messages::outgoing::new_config_class::entity_scheme;
    using trace_result_type  = messages::outgoing::traces;
    using session_state_type = messages::outgoing::session_state;

   public:
    explicit session_context(connection_ptr conn);
    void login(std::string_view id, std::string_view pw);
    void push_command(std::string_view command);
    void configure(std::string_view class_key, uint64_t key, nlohmann::json const& new_value);
    auto signal_fetch_trace(std::string_view trace)
            -> std::future<messages::outgoing::traces>;
    void control_trace(
            std::string_view class_name, uint64_t key, bool const* subscr, bool const* fold);
    auto suggest_command(std::string command, int16_t position)
            -> std::future<messages::outgoing::suggest_command>;

    auto check_trace_class_change() -> std::vector<std::pair<std::string, uint64_t>> const*;
    session_connection_state status() const noexcept { return _conn->status(); }
    info_type const* info() const noexcept;
    auto const& configs() const noexcept { return _configs; }

    std::string_view shell_output(size_t* fence = nullptr) const
    {
        if (fence)
        {
            assert_(*fence <= _output_fence);

            auto diff = _output_fence - *fence;
            diff      = std::min(diff, _output.size());
            *fence    = _output_fence;
            return std::string_view{_output}.substr(_output.size() - diff);
        }
        else
        {
            return _output;
        }
    }

    bool consume_recv_char() { return not _shell_latest.test_and_set(); }

   private:
    void _on_recv(std::string_view route, nlohmann::json const& msg);

    template <class MsgTy_, typename Handler_>
    void _install(Handler_&& h)
    {
        auto wrapped_handler{
                [h = std::forward<Handler_>(h), message = MsgTy_{}]  //
                (nlohmann::json const& object) mutable {
                    try
                    {
                        object.get_to(message);
                        h(message);
                    }
                    catch (std::exception&)
                    {
                        throw session_context_message_marshal_error{};
                    }
                }};

        auto is_new = _handlers.try_emplace(
                                       perfkit::hasher::fnv1a_64(
                                               MsgTy_::ROUTE,
                                               MsgTy_::ROUTE + strlen(MsgTy_::ROUTE)),
                                       std::move(wrapped_handler))
                              .second;

        assert_(is_new);
    }

    void _on_epoch(info_type& payload);
    void _on_session_state(messages::outgoing::session_state const& payload) const;
    void _on_shell_output(messages::outgoing::shell_output const& payload);
    void _on_new_config_class(messages::outgoing::new_config_class const& payload);
    void _on_config_entity_update(messages::outgoing::config_entity const& payload);
    void _on_suggest_result(messages::outgoing::suggest_command const& payload);
    void _on_trace_list(messages::outgoing::trace_class_list const& payload);
    void _on_trace(messages::outgoing::traces const& payload);

   public:
    perfkit::function<void(session_state_type const&)>
            on_session_state_update{perfkit::default_function};

   private:
    connection_ptr _conn;

    std::string _output;
    size_t _output_fence = 0;

    std::optional<info_type> _info;
    std::atomic_flag _shell_latest;

    int64_t _waiting_suggest = 0;
    std::optional<std::promise<messages::outgoing::suggest_command>> _suggest_promise;

    std::unordered_map<
            uint64_t,
            perfkit::function<void(nlohmann::json const&)>>
            _handlers;

    std::map<std::string, config_type, std::less<>>
            _configs;

    std::unordered_map<uint64_t, config_entity_type*>
            _entity_indexes;

    bool _trace_class_dirty = false;
    std::vector<std::pair<std::string, uint64_t>> _trace_classes;
    std::map<
            std::string,
            std::promise<messages::outgoing::traces>,
            std::less<>>
            _pending_trace_results;
};
