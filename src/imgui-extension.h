#pragma once
#include <string_view>

#include "imgui.h"

namespace ImGui {
double GetGlobalTime();

bool Spinner(
        const char* label,
        const ImU32& color,
        float radius  = 5.f,
        int thickness = 2,
        double time   = GetGlobalTime());

void LoadingIndicatorCircle(
        const char* label,
        const ImVec4& main_color, const ImVec4& backdrop_color,
        float indicator_radius = 6.,
        int circle_count = 6, float speed = 2.);

void InputTextLeft(const char* label,
                   const char* hint,
                   char* buf,
                   size_t bufSize,
                   ImGuiInputTextFlags flags       = 0,
                   ImGuiInputTextCallback callback = nullptr,
                   void* userData                  = nullptr);
}  // namespace ImGui
