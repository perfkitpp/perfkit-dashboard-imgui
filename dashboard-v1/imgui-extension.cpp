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

#include "imgui-extension.h"

#include <algorithm>
#include <cmath>

#include "imgui_internal.h"

namespace ImGui {
bool Spinner(const char* label, const ImU32& color, float radius, int thickness, double time)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g         = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id        = window->GetID(label);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size((radius)*2, (radius + style.FramePadding.y) * 2);

    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    // Render
    window->DrawList->PathClear();

    int num_segments = 30;
    int start        = abs(ImSin(time * 1.8f) * (num_segments - 5));

    const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
    const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

    const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

    for (int i = 0; i < num_segments; i++)
    {
        const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + time * 8) * radius,
                                            centre.y + ImSin(a + time * 8) * radius));
    }

    window->DrawList->PathStroke(color, false, thickness);
    return true;
}

void LoadingIndicatorCircle(const char* label,
                            const ImVec4& main_color, const ImVec4& backdrop_color,
                            const float indicator_radius,
                            const int circle_count, const float speed)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
    {
        return;
    }

    ImGuiContext& g  = *GImGui;
    const ImGuiID id = window->GetID(label);

    const ImVec2 pos          = window->DC.CursorPos;
    const float circle_radius = indicator_radius / 10.0f;
    const ImRect bb(pos, ImVec2(pos.x + indicator_radius * 2.0f,
                                pos.y + indicator_radius * 2.0f));
    auto& style = ImGui::GetStyle();
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
    {
        return;
    }

    const float t            = g.Time;
    const auto degree_offset = 2.0f * IM_PI / circle_count;

    for (int i = 0; i < circle_count; ++i)
    {
        const auto x      = indicator_radius * std::sin(degree_offset * i);
        const auto y      = indicator_radius * std::cos(degree_offset * i);
        const auto growth = std::max(0.0f, std::sin(t * speed - i * degree_offset));
        ImVec4 color;
        color.x = main_color.x * growth + backdrop_color.x * (1.0f - growth);
        color.y = main_color.y * growth + backdrop_color.y * (1.0f - growth);
        color.z = main_color.z * growth + backdrop_color.z * (1.0f - growth);
        color.w = 1.0f;
        window->DrawList->AddCircleFilled(ImVec2(pos.x + indicator_radius + x,
                                                 pos.y + indicator_radius - y),
                                          circle_radius + growth * circle_radius,
                                          GetColorU32(color));
    }
}

void InputTextLeft(const char* label,
                   const char* hint,
                   char* buf,
                   size_t bufSize,
                   ImGuiInputTextFlags flags,
                   ImGuiInputTextCallback callback,
                   void* userData)
{
    ImGui::Text(label);
    ImGui::SameLine();
    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint(label, hint, buf, bufSize, flags, callback, userData);
    ImGui::PopItemWidth();
}

double GetGlobalTime()
{
    return GImGui->Time;
}
}  // namespace ImGui