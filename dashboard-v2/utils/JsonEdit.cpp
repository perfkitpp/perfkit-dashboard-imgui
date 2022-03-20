//
// Created by ki608 on 2022-03-20.
//

#include "JsonEdit.hpp"

#include <nlohmann/json.hpp>

#include "imgui.h"
#include "imgui_internal.h"

ImU32 ImGui::ContentColorByJsonType(nlohmann::detail::value_t type)
{
    switch (type)
    {
        case nlohmann::detail::value_t::null:
        case nlohmann::detail::value_t::binary:
        case nlohmann::detail::value_t::boolean:
            return ColorRefs::GlyphKeyword;

        case nlohmann::detail::value_t::discarded:
            return ColorRefs::FrontError;

        case nlohmann::detail::value_t::array:
        case nlohmann::detail::value_t::object:
            return ColorRefs::GlyphUserType;

        case nlohmann::detail::value_t::string:
            return ColorRefs::GlyphString;

        case nlohmann::detail::value_t::number_integer:
        case nlohmann::detail::value_t::number_unsigned:
        case nlohmann::detail::value_t::number_float:
            return ColorRefs::GlpyhNumber;

        default:
            return ColorRefs::FrontError;
    }
}

bool ImGui::SingleLineJsonEdit(char const* str_id, nlohmann::json& value, const string& cacheStr, bool bConfirmEnter)
{
    IM_ASSERT(value.is_number() || value.is_boolean());

    if (value.is_boolean())
        return ImGui::Checkbox(str_id, value.get_ptr<bool*>());

    ImGuiContext& g = *GImGui;
    ImGuiWindow*  window = g.CurrentWindow;
    ImVec2        pos_before = window->DC.CursorPos;

    PushID(str_id);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(g.Style.ItemSpacing.x, g.Style.FramePadding.y * 2.0f));
    bool ret = Selectable("##Selectable", false, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowItemOverlap);
    PopStyleVar();

    ImGuiID id = window->GetID("##Input");
    bool    temp_input_is_active = TempInputIsActive(id);
    bool    temp_input_start = ret ? IsMouseDoubleClicked(0) : false;

    if (temp_input_start)
        SetActiveID(id, window);

    ImGui::PushStyleColor(ImGuiCol_Text, ContentColorByJsonType(value.type()));

    if (temp_input_is_active || temp_input_start)
    {
        void*       ptr = nullptr;
        int         dataType = 0;
        char const* format = 0;

        switch (value.type())
        {
            case nlohmann::detail::value_t::number_integer:
                ptr = value.get_ptr<int64_t*>();
                dataType = ImGuiDataType_S64;
                format = "%lld";
                break;

            case nlohmann::detail::value_t::number_unsigned:
                ptr = value.get_ptr<uint64_t*>();
                dataType = ImGuiDataType_U64;
                format = "%llu";
                break;

            case nlohmann::detail::value_t::number_float:
                ptr = value.get_ptr<double*>();
                dataType = ImGuiDataType_Double;
                format = "%f";
                break;

            default:
                break;
        }

        IM_ASSERT(dataType != 0);
        IM_ASSERT(!!ptr);

        ImVec2 pos_after = window->DC.CursorPos;
        window->DC.CursorPos = pos_before;
        ret = ImGui::TempInputScalar(ImRect(GetItemRectMin(), GetItemRectMax()), id, "##Input", dataType, ptr, format);
        ret = (ret && not bConfirmEnter) || (bConfirmEnter && ImGui::IsKeyPressed(ImGuiKey_Enter, false));

        window->DC.CursorPos = pos_after;
    }
    else
    {
        ret = false;
        ImGui::SameLine(0, 0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(cacheStr.c_str());
    }

    ImGui::PopStyleColor();

    PopID();
    return ret;
}
