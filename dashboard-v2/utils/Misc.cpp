//
// Created by ki608 on 2022-03-18.
//

#include "Misc.hpp"

#include <cmath>

#include <spdlog/fmt/fmt.h>

#include "TextEditor.h"

void xterm_leap_escape(TextEditor* edit, std::string_view content)
{
    if (content.empty())
        return;

    size_t index = 0;
    bool is_escape = false;
    std::string buffer;

    auto line_begin = edit->GetTotalLines();
    auto is_read_only = edit->IsReadOnly();
    edit->SetReadOnly(false);

    for (; not content.empty() && index < content.size(); ++index) {
        if (is_escape && content[index] == 'm') {
            content = content.substr(index + 1);
            index = 0;

            is_escape = false;
        } else if (content[index] == '\033') {
            is_escape = true;

            buffer = content.substr(0, index);

            content = content.substr(index + 1);
            index = 0;

            edit->AppendTextAtEnd(buffer.c_str());
        }
    }

    if (not content.empty()) {
        buffer = content;
        edit->AppendTextAtEnd(buffer.c_str());
    }

    edit->SetReadOnly(is_read_only);
}

char const* FormatBitText(int64_t value, bool bBits, bool bSpeed, int64_t* valueOut, char const** suffixOut)
{
    static const auto locale = std::locale("en-us");
    static char buf[128];
    constexpr char const* SUFFIXES[2][2][6]  // bBits, bSpeed, step
            = {
                    {
                            {"B", "kiB", "MiB", "GiB", "TiB", "PiB"},
                            {"B/s", "kiB/s", "MiB/s", "GiB/s", "TiB/s", "PiB/s"},
                    },
                    {
                            {"bits", "kbits", "Mbits", "Gbits", "Tbits", "PBits"},
                            {"bps", "kbps", "Mbps", "Gbps", "Tbps", "Pbps"},
                    },
            };

    if (bBits) { value <<= 3; }

    auto& suffixes = SUFFIXES[bBits][bSpeed];
    int sufidx = std::clamp<int>((log2(value) - 8) / 10, 0, 6);
    int numShift = sufidx * 10;

    value >>= numShift;
    if (valueOut) { *valueOut = value; }
    if (suffixOut) { *suffixOut = suffixes[sufidx]; }

    auto end = fmt::format_to(buf, locale, "{:L} {}", value, suffixes[sufidx]);
    *end = 0;

    return buf;
}
