#include "session_context.hpp"

#include "messages.hpp"
#include "perfkit/common/algorithm/base64.hxx"
#include "perfkit/common/hasher.hxx"
#include "perfkit/common/macros.hxx"
#include "picosha2.h"

using namespace perfkit::terminal::net;

session_context::session_context(connection_ptr conn)
        : _conn(std::move(conn))
{
    _conn->received_message_handler = CPPH_BIND(_on_recv);
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

bool session_context::expired() const
{
    return not _conn->connection_valid();
}

template <size_t N_>
static constexpr uint64_t STRCASE(char const (&str)[N_])
{
    return perfkit::hasher::fnv1a_64(std::begin(str), std::end(str));
}

void session_context::_on_recv(std::string_view route, nlohmann::json const& msg)
{
    switch (perfkit::hasher::fnv1a_64(route))
    {
        case STRCASE("update:error"):
            break;
        case STRCASE("epoch"):
            break;

        default:
            break;
    }
}
