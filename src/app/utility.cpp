//
// Created by ki608 on 2021-12-19.
//

#include "utility.hpp"

#include "TextEditor.h"

void xterm_leap_escape(TextEditor *edit, std::string_view content)
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
