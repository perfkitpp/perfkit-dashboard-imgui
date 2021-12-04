#include "session_context.hpp"

#include <spdlog/spdlog.h>

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
        auto hash = perfkit::hasher::fnv1a_64(route);
        _handlers.at(hash)(msg);
    }
    catch (std::out_of_range&)
    {
        SPDLOG_ERROR("invalid protoocol for route {}", route);
    }
}

void session_context::_on_epoch(info_type& payload)
{
    _info.emplace(std::move(payload));

    _output.use(
            [](auto&& e)
            {
                e.clear();
            });
}

void session_context::_on_shell_output(messages::outgoing::shell_output const& message)
{
    auto& str = message.content;

    _output.use(
            [&](std::string& s)
            {
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
            });

    _shell_latest.clear();
}
