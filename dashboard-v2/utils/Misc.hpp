//
// Created by ki608 on 2022-03-18.
//

#pragma once

#include <string_view>

class TextEditor;
void xterm_leap_escape(TextEditor* edit, std::string_view content);

/**
 * bps -> Kbps -> Mbps -> Gbps -> Tbps
 * b -> Kb -> Mb -> Gb -> Tb
 * B -> KiB -> MiB -> GiB -> TiB ...
 * B/s -> KiB/s -> MiB/s -> GiB/s -> TiB/s
 */
char const* FormatBitText(
        int64_t      value,
        bool         bBits = true,
        bool         bSpeed = true,
        int64_t*     valueOut = nullptr,
        char const** suffixOut = nullptr);
