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
#include <algorithm>
#include <string>
#include <string_view>

#include "imgui.h"

namespace ImGui {
double GetGlobalTime();

bool   Spinner(
          const char*  label,
          const ImU32& color,
          float        radius    = 5.f,
          int          thickness = 2,
          double       time      = GetGlobalTime());

void LoadingIndicatorCircle(
        const char*   label,
        const ImVec4& main_color, const ImVec4& backdrop_color,
        float indicator_radius = 6.,
        int circle_count = 6, float speed = 2.);

void InputTextLeft(const char*            label,
                   const char*            hint,
                   char*                  buf,
                   size_t                 bufSize,
                   ImGuiInputTextFlags    flags    = 0,
                   ImGuiInputTextCallback callback = nullptr,
                   void*                  userData = nullptr);

bool BeginChildAutoHeight(char const* key, float width = 0., ImGuiWindowFlags flags = 0);
void EndChildAutoHeight(const char* key);

class ScopedChildWindow
{
    char _buf[256];
    bool _draw = false;

   public:
    explicit ScopedChildWindow(std::string_view key, float width = 0., ImGuiWindowFlags flags = 0) noexcept
    {
        _buf[255] = 0;
        strncpy(_buf, key.data(), std::min(sizeof _buf - 1, key.size()));
        _draw = BeginChildAutoHeight(_buf, width, flags);
    }

    ~ScopedChildWindow() noexcept
    {
        EndChildAutoHeight(_buf);
    }

    operator bool() const noexcept { return _draw; }
};

void        PushStatefulColors(ImGuiCol idx, ImVec4 const& color);
void        PushStatefulColorsUni(ImGuiCol idx, ImVec4 const& color);
void        PopStatefulColors();

inline void PushStatefulColors(ImGuiCol idx, ImU32 color) { PushStatefulColors(idx, ImGui::ColorConvertU32ToFloat4(color)); }
inline void PushStatefulColorsUni(ImGuiCol idx, ImU32 color) { PushStatefulColorsUni(idx, ImGui::ColorConvertU32ToFloat4(color)); }

}  // namespace ImGui