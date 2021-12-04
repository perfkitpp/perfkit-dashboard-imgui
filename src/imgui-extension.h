#pragma once
#include "imgui.h"

namespace ImGui
{
bool Spinner(const char* label, const ImU32& color, float radius = 5.f, int thickness = 2);

void LoadingIndicatorCircle(
        const char* label,
        const ImVec4& main_color, const ImVec4& backdrop_color,
        const float indicator_radius = 6.,
        const int circle_count = 6, const float speed = 2.);
}  // namespace ImGui
