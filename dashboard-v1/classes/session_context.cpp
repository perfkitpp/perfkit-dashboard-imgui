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

#include "session_context.hpp"

#include <spdlog/spdlog.h>

#include "app/application.hpp"
#include "cpph/algorithm/base64.hxx"
#include "cpph/helper/macros.hxx"
#include "messages.hpp"
#include "picosha2.h"

using namespace perfkit::terminal::net;

session_context::session_context(connection_ptr conn)
        : _conn(std::move(conn))
{
    _conn->received_message_handler = CPPH_BIND(_on_recv);

    _install<outgoing::shell_output>(CPPH_BIND(_on_shell_output));
    _install<outgoing::session_reset>(CPPH_BIND(_on_epoch));
    _install<outgoing::new_config_class>(CPPH_BIND(_on_new_config_class));
    _install<outgoing::config_entity>(CPPH_BIND(_on_config_entity_update));
    _install<outgoing::suggest_command>(CPPH_BIND(_on_suggest_result));
    _install<outgoing::trace_class_list>(CPPH_BIND(_on_trace_list));
    _install<outgoing::traces>(CPPH_BIND(_on_trace));
    _install<outgoing::session_state>(CPPH_BIND(_on_session_state));
}

void session_context::login(std::string_view id, std::string_view pw)
{
    nlohmann::json param;
    param["id"] = id;

    std::array<char, 32> array;
    picosha2::hash256(pw, array);

    std::array<char, perfkit::base64::encoded_size(sizeof(array))> b64hash = {};
    perfkit::base64::encode(array, b64hash.begin());

    param["pw"] = std::string_view{b64hash.data(), b64hash.size()};
    _conn->send_message("auth:login", param);
}

void session_context::push_command(std::string_view command)
{
    incoming::push_command cmd;
    cmd.command = command;
    _conn->send(cmd);
}

void session_context::configure(
        std::string_view class_key,
        uint64_t key,
        nlohmann::json const& new_value)
{
    incoming::configure_entity conf;
    conf.class_key = class_key;
    conf.content.push_back({key, new_value});
    _conn->send(std::move(conf));
}

auto session_context::suggest_command(std::string command, int16_t position)
        -> std::future<messages::outgoing::suggest_command>
{
    incoming::suggest_command cmd;
    cmd.reply_to = ++_waiting_suggest;
    cmd.position = position;
    cmd.command  = std::move(command);
    _conn->send(std::move(cmd));

    _suggest_promise.reset();
    return _suggest_promise.emplace().get_future();
}

session_context::info_type const* session_context::info() const noexcept
{
    return _info.has_value() ? &_info.value() : nullptr;
}

template <size_t N_>
static constexpr uint64_t STRCASE(char const (&str)[N_])
{
    return perfkit::hasher::fnv1a_64(std::begin(str), std::end(str));
}

void session_context::_on_recv(std::string_view route, nlohmann::json const& msg)
{
    try
    {
        application::post_event(
                [this,
                 hash = perfkit::hasher::fnv1a_64(route),
                 msg,
                 alive_marker = std::weak_ptr{_conn}] {
                    try
                    {
                        if (not alive_marker.expired())
                            _handlers.at(hash)(msg);
                    }
                    catch (std::out_of_range&)
                    {
                        SPDLOG_ERROR("undefined event type received. contents: {}", msg.dump(2));
                    }
                });
    }
    catch (std::out_of_range&)
    {
        SPDLOG_ERROR("invalid protoocol for route {}", route);
    }
}

void session_context::_on_epoch(info_type& payload)
{
    _info.emplace(std::move(payload));

    _output.clear();
    _configs.clear();
    _entity_indexes.clear();
}

void session_context::_on_shell_output(messages::outgoing::shell_output const& message)
{
    auto& str = message.content;
    auto& s   = _output;

    enum
    {
        BUF_ERASE_SIZE  = 2 << 20,
        BUF_RETAIN_SIZE = 1 << 20
    };

    if (s.size() + str.size() > (BUF_ERASE_SIZE)
        && s.size() > BUF_RETAIN_SIZE)
    {
        auto to_erase = s.size() % BUF_RETAIN_SIZE;
        s.erase(s.begin(), s.begin() + to_erase);
    }

    s.append(str);
    _output_fence += str.size();
    _shell_latest.clear();
}

template <typename Target_, typename Fn_>
static void recurse_category(
        Target_&& target,
        Fn_ const& visitor)
{
    for (auto& category : target.subcategories)
    {
        recurse_category(category, visitor);
    }

    for (auto& elem : target.entities)
    {
        visitor(elem);
    }
}

static uint64_t make_key(std::string_view s, uint64_t hash)
{
    return perfkit::hasher::fnv1a_64(s, hash);
}

void session_context::_on_new_config_class(messages::outgoing::new_config_class const& payload)
{
    if (auto it = _configs.find(payload.key); it != _configs.end())
    {  // erase existing indexings.
        recurse_category(
                payload.root,
                [&](config_entity_type const& e) {
                    _entity_indexes.erase(make_key(payload.key, e.config_key));
                });

        _configs.erase(it);
        SPDLOG_INFO("existing config class '{}' erased", payload.key);
    }

    auto [it, _] = _configs.emplace(payload.key, payload.root);
    recurse_category(
            it->second,
            [&](config_entity_type& e) {
                auto [it_elem, is_new]
                        = _entity_indexes.try_emplace(
                                make_key(payload.key, e.config_key), &e);

                if (not is_new)
                {
                    SPDLOG_WARN("logic error: duplicated config entity hash");
                }
            });

    SPDLOG_INFO("new config class '{}' recognized", payload.key);
}

void session_context::_on_config_entity_update(messages::outgoing::config_entity const& payload)
{
    if (payload.content.empty())
        return;

    for (auto& update : payload.content)
    {
        auto it = _entity_indexes.find(make_key(payload.class_key, update.config_key));

        if (it == _entity_indexes.end())
        {
            SPDLOG_WARN("unrecognized entity index from registry {}", payload.class_key);
            continue;
        }

        // apply update
        it->second->value = update.value;
    }
}

void session_context::_on_suggest_result(messages::outgoing::suggest_command const& payload)
{
    if (payload.reply_to == _waiting_suggest)
    {
        _suggest_promise->set_value(payload);
    }
}

auto session_context::signal_fetch_trace(std::string_view trace) -> std::future<messages::outgoing::traces>
{
    messages::incoming::signal_fetch_traces message;
    message.targets.emplace_back(trace);
    _conn->send(std::move(message));

    auto it = _pending_trace_results.lower_bound(trace);
    if (it != _pending_trace_results.end() && it->first == trace)
    {
        // invalidate existing promise before set value
        it->second = {};
    }
    else
    {
        it = _pending_trace_results.emplace_hint(
                it, std::string{trace}, std::promise<messages::outgoing::traces>{});
    }

    return it->second.get_future();
}

void session_context::_on_trace_list(const outgoing::trace_class_list& payload)
{
    _trace_classes.assign(payload.content.begin(), payload.content.end());
    _trace_class_dirty = true;
}

void session_context::_on_trace(const outgoing::traces& payload)
{
    auto it = _pending_trace_results.find(payload.class_name);
    if (it == _pending_trace_results.end())
        return;

    it->second.set_value(payload);
    _pending_trace_results.erase(it);
}
auto session_context::check_trace_class_change() -> std::vector<std::pair<std::string, uint64_t>> const*
{
    if (_trace_class_dirty)
    {
        _trace_class_dirty = false;
        return &_trace_classes;
    }
    else
    {
        return nullptr;
    }
}

void session_context::control_trace(
        std::string_view class_name, uint64_t key, const bool* subscr, const bool* fold)
{
    perfkit::terminal::net::incoming::control_trace message;
    message.class_name = class_name;
    message.trace_key  = key;

    if (fold) { message.fold = *fold; }
    if (subscr) { message.subscribe = *subscr; }

    _conn->send(std::move(message));
}

void session_context::_on_session_state(const outgoing::session_state& payload) const
{
    on_session_state_update(payload);
}
