#include "session_context.hpp"

#include <spdlog/spdlog.h>

#include "app/application.hpp"
#include "messages.hpp"
#include "perfkit/common/algorithm/base64.hxx"
#include "perfkit/common/macros.hxx"
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
    _install<outgoing::suggest_command>(CPPH_BIND(on_suggest_result));
}

void session_context::login(std::string_view id, std::string_view pw)
{
    nlohmann::json param;
    param["id"] = id;

    std::array<char, 256> array;
    picosha2::hash256(pw, array);

    std::array<char, perfkit::base64::encoded_size(256)> b64hash;
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
                    if (not alive_marker.expired())
                        _handlers.at(hash)(msg);
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

void session_context::on_suggest_result(messages::outgoing::suggest_command const& payload)
{
    if (payload.reply_to == _waiting_suggest)
    {
        _suggest_promise->set_value(payload);
    }
}
