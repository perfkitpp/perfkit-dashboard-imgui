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

#include "imgui_extension.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <string_view>

#include "imgui_internal.h"

namespace ImGui {
bool Spinner(const char* label, ImU32 color, float radius, int thickness, double time)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext&     g     = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID     id    = window->GetID(label);

    ImVec2            pos   = window->DC.CursorPos;
    ImVec2            size((radius)*2, (radius + style.FramePadding.y) * 2);

    const ImRect      bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    // Render
    window->DrawList->PathClear();

    int          num_segments = 30;
    int          start        = abs(ImSin(time * 1.8f) * (num_segments - 5));

    const float  a_min        = IM_PI * 2.0f * ((float)start) / (float)num_segments;
    const float  a_max        = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

    const ImVec2 centre       = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

    for (int i = 0; i < num_segments; i++)
    {
        const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
        window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + time * 8) * radius,
                                            centre.y + ImSin(a + time * 8) * radius));
    }

    float alpha = ((color & 0xff'000000) >> 24) / 255.f;
    alpha *= style.Alpha;
    color &= 0x00'ffffff;
    color |= (lround(alpha * 255.f) << 24);
    window->DrawList->PathStroke(color, false, thickness);
    return true;
}

void LoadingIndicatorCircle(const char*   label,
                            const ImVec4& main_color, const ImVec4& backdrop_color,
                            const float indicator_radius,
                            const int circle_count, const float speed)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
    {
        return;
    }

    ImGuiContext& g             = *GImGui;
    const ImGuiID id            = window->GetID(label);

    const ImVec2  pos           = window->DC.CursorPos;
    const float   circle_radius = indicator_radius / 10.0f;
    const ImRect  bb(pos, ImVec2(pos.x + indicator_radius * 2.0f,
                                 pos.y + indicator_radius * 2.0f));
    auto&         style = ImGui::GetStyle();
    ItemSize(bb, style.FramePadding.y);
    if (!ItemAdd(bb, id))
    {
        return;
    }

    const float t             = g.Time;
    const auto  degree_offset = 2.0f * IM_PI / circle_count;

    for (int i = 0; i < circle_count; ++i)
    {
        const auto x      = indicator_radius * std::sin(degree_offset * i);
        const auto y      = indicator_radius * std::cos(degree_offset * i);
        const auto growth = std::max(0.0f, std::sin(t * speed - i * degree_offset));
        ImVec4     color;
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

void InputTextLeft(const char*            label,
                   const char*            hint,
                   char*                  buf,
                   size_t                 bufSize,
                   ImGuiInputTextFlags    flags,
                   ImGuiInputTextCallback callback,
                   void*                  userData)
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

float* findStorage(std::string_view buf)
{
    static auto _storage = std::map<std::string, float, std::less<>>{};

    auto        iter     = _storage.find(buf);
    if (iter == _storage.end())
    {
        iter = _storage.try_emplace(std::string{buf}, 0.f).first;
    }

    return &iter->second;
}

bool BeginChildAutoHeight(const char* key, float width, bool bBorder, ImGuiWindowFlags flags)
{
    auto heightPtr = findStorage(key);
    if (*heightPtr == 0) { *heightPtr = 1.f; };
    auto height = *heightPtr + ImGui::GetStyle().WindowPadding.y - 2;
    return ImGui::BeginChild(key, {width, height}, bBorder, flags);
}

void EndChildAutoHeight(const char* key, bool bWasDrawn)
{
    float height = GetCursorPosY();

    ImGui::EndChild();

    if (bWasDrawn) { *findStorage(key) = height; }
}

void PushStatefulColors(ImGuiCol idx, ImVec4 const& color)
{
    auto& base   = ImGui::GetStyleColorVec4(idx);
    auto& state1 = ImGui::GetStyleColorVec4(idx + 1);
    auto& state2 = ImGui::GetStyleColorVec4(idx + 2);

    ImGui::PushStyleColor(idx + 0, color);
    ImGui::PushStyleColor(idx + 1, color + state1 - base);
    ImGui::PushStyleColor(idx + 2, color + state2 - base);
}

void PopStatefulColors()
{
    ImGui::PopStyleColor(3);
}

void PushStatefulColorsUni(ImGuiCol idx, const ImVec4& color)
{
    ImGui::PushStyleColor(idx, color);
    ImGui::PushStyleColor(idx + 1, color);
    ImGui::PushStyleColor(idx + 2, color);
}

char const* RetrieveCurrentWindowName()
{
    return ImGui::GetCurrentWindow()->Name;
}

//! @see https://github.com/ocornut/imgui/issues/1537#issuecomment-780262461
void ToggleButton(const char* str_id, bool* v)
{
    ImVec2      p         = ImGui::GetCursorScreenPos();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float       height    = ImGui::GetFrameHeight();
    float       width     = height * 1.55f;
    float       radius    = height * 0.50f;

    ImGui::InvisibleButton(str_id, ImVec2(width, height));
    if (ImGui::IsItemClicked())
        *v = !*v;

    float         t          = *v ? 1.0f : 0.0f;
    ImGuiContext& g          = *GImGui;

    float         ANIM_SPEED = 0.08f;
    if (g.LastActiveId == g.CurrentWindow->GetID(str_id))  // && g.LastActiveIdTimer < ANIM_SPEED)
    {
        float t_anim = ImSaturate(g.LastActiveIdTimer / ANIM_SPEED);
        t            = *v ? (t_anim) : (1.0f - t_anim);
    }

    ImU32 col_bg;
    if (ImGui::IsItemHovered())
        col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.78f, 0.78f, 0.78f, 1.0f), ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), t));
    else
        col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive), t));

    draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);
    draw_list->AddCircleFilled(ImVec2(p.x + radius + t * (width - radius * 2.0f), p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));
}

bool SelectableInput(const char* str_id, bool selected, ImGuiSelectableFlags flags, char* buf, size_t buf_size)
{
    ImGuiContext& g          = *GImGui;
    ImGuiWindow*  window     = g.CurrentWindow;
    ImVec2        pos_before = window->DC.CursorPos;

    PushID(str_id);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(g.Style.ItemSpacing.x, g.Style.FramePadding.y * 2.0f));
    bool ret = Selectable("##Selectable", selected, flags | ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowItemOverlap);
    PopStyleVar();

    ImGuiID id                   = window->GetID("##Input");
    bool    temp_input_is_active = TempInputIsActive(id);
    bool    temp_input_start     = ret ? IsMouseDoubleClicked(0) : false;

    if (temp_input_start)
        SetActiveID(id, window);

    if (temp_input_is_active || temp_input_start)
    {
        ImVec2 pos_after     = window->DC.CursorPos;
        window->DC.CursorPos = pos_before;
        ret                  = TempInputText(ImRect(GetItemRectMin(), GetItemRectMax()), id, "##Input", buf, (int)buf_size, ImGuiInputTextFlags_None);
        window->DC.CursorPos = pos_after;
    }
    else
    {
        window->DrawList->AddText(pos_before, GetColorU32(ImGuiCol_Text), buf);
    }

    PopID();
    return ret;
}

bool InputText(const char* str_id, std::string& str, ImGuiInputTextFlags flags, ImGuiInputTextCallback cb, void* user)
{
    using std::string;
    bool bValueChanged = false;
    struct context_t
    {
        string*                str;
        size_t                 text_len;
        ImGuiInputTextCallback cb_org;
        void*                  user_org;
    };
    auto ctx      = context_t{&str, str.size(), cb, user};

    bValueChanged = ImGui::InputText(
            "##TEDIT",
            str.data(),
            str.size() + 1,
            flags | ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* cbdata) {
                auto ctx = (context_t*)cbdata->UserData;

                if (cbdata->EventFlag == ImGuiInputTextFlags_CallbackResize)
                {
                    auto str = ctx->str;
                    str->resize(str->size() + 64);

                    cbdata->BufSize  = str->size();
                    cbdata->Buf      = str->data();
                    cbdata->BufDirty = true;
                    ctx->text_len    = cbdata->BufTextLen;
                }
                else if (ctx->cb_org)
                {
                    cbdata->UserData = ctx->user_org;
                    return ctx->cb_org(cbdata);
                }

                return 0;
            },
            &ctx);

    str.resize(ctx.text_len);
    return bValueChanged;
}

void SplitRenders(
        float* spanSize, float axisSize,
        ImGuiID firstID, ImGuiID secondID,
        std::function<void()> const& first, std::function<void()> const& second,
        int flags, int firstWndFlags, int secondWndFlags)
{
    auto handleThickness     = ImGui::GetFontSize() * DpiScale() * 0.72f;
    auto halfHandleThick     = handleThickness * 0.5f;
    *spanSize                = max(ImGui::GetFontSize() * DpiScale() * 1.7f, *spanSize);

    bool const bIsHorizontal = not(flags & ImGuiSplitRenderFlags_Vertical);
    bool const bPivotFirst   = flags & ImGuiSplitRenderFlags_PivotFirst;
    auto const cursorStartG  = ImGui::GetCursorScreenPos();

    ImVec2     size{*spanSize * (bPivotFirst ? 1 : -1), axisSize};
    ImVec2     spaceSize = ImGui::GetContentRegionAvail();

    if (bIsHorizontal) { size.x = min(size.x, spaceSize.x - handleThickness); }
    if (not bIsHorizontal) { swap(size.x, size.y), size.y = min(size.y, spaceSize.y - handleThickness); }

    ImGui::BeginChild(firstID, size, firstWndFlags & ImGuiSplitRenderFlags_DrawBorderFirst, firstWndFlags);
    first();
    ImGui::EndChild();

    if (bIsHorizontal) { ImGui::SameLine(0, 0); }

    size.x = handleThickness;
    size.y = axisSize == 0 ? -1 : axisSize;
    if (not bIsHorizontal) { swap(size.x, size.y); }

    ImGui::PushID((uint8_t*)spanSize + 1),
            ImGui::PushStyleColor(ImGuiCol_Button, 0),
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    bool bIsHeld = false, bIsHover = false;

    ImGui::Button("##SEL", size);
    ImGui::ButtonBehavior({ImGui::GetItemRectMin(), ImGui::GetItemRectMax()}, ImGui::GetID("##SEL"), &bIsHover, &bIsHeld);

    ImGui::PopID(),
            ImGui::PopStyleColor(),
            ImGui::PopStyleVar();

    if (bIsHover || bIsHeld)
    {
        ImGui::SetMouseCursor(bIsHorizontal ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    }

    if (bIsHeld)
    {
        auto& io  = ImGui::GetIO();
        auto  pos = io.MousePos - cursorStartG;
        auto  hh  = halfHandleThick;

        if (bIsHorizontal)
            *spanSize = bPivotFirst ? pos.x - hh : spaceSize.x - pos.x + hh;
        else
            *spanSize = bPivotFirst ? pos.y - hh : spaceSize.y - pos.y + hh;
    }

    if (bIsHorizontal) { ImGui::SameLine(0, 0); }
    size.x = 0;  // fill remaining area
    size.y = axisSize;

    if (not bIsHorizontal) { swap(size.x, size.y); }

    ImGui::BeginChild(secondID, size, secondWndFlags & ImGuiSplitRenderFlags_DrawBorderSecond, secondWndFlags);
    second();
    ImGui::EndChild();
}
}  // namespace ImGui
