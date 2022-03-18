//
// Created by ki608 on 2022-03-18.
//

#pragma once

#include <string_view>

class TextEditor;
void xterm_leap_escape(TextEditor* edit, std::string_view content);
