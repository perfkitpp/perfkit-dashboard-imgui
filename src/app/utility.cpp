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

//
// Created by ki608 on 2021-12-19.
//

#include "utility.hpp"

#include "TextEditor.h"

void xterm_leap_escape(TextEditor* edit, std::string_view content)
{
    if (content.empty())
        return;

    size_t index   = 0;
    bool is_escape = false;
    std::string buffer;

    auto line_begin   = edit->GetTotalLines();
    auto is_read_only = edit->IsReadOnly();
    edit->SetReadOnly(false);

    for (; not content.empty() && index < content.size(); ++index)
    {
        if (is_escape && content[index] == 'm')
        {
            content = content.substr(index + 1);
            index   = 0;

            is_escape = false;
        }
        else if (content[index] == '\033')
        {
            is_escape = true;

            buffer = content.substr(0, index);

            content = content.substr(index + 1);
            index   = 0;

            edit->AppendTextAtEnd(buffer.c_str());
        }
    }

    if (not content.empty())
    {
        buffer = content;
        edit->AppendTextAtEnd(buffer.c_str());
    }

    edit->SetReadOnly(is_read_only);
}
