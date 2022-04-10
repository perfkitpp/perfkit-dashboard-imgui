//
// Created by ki608 on 2022-03-20.
//

#pragma once
#include <nlohmann/json_fwd.hpp>

#include "imgui.h"

class JsonEditor
{
   public:
    struct EditorStateFlag
    {
        bool bIsDirty      : 1;
        bool bIsFocused    : 1;

        bool bEvtLostFocus : 1;
    };

   private:
    using Json = nlohmann::json;

   private:
    unique_ptr<Json> _editing;
    unique_ptr<Json> _opt_min;
    unique_ptr<Json> _opt_max;
    EditorStateFlag  _flags = {};

   public:
    void Reset(Json&& object, Json const* min = {}, Json const* max = {}) {}
    void Render(char const* id) {}

    auto FlagsThisFrame() const -> EditorStateFlag { return _flags; }
    void ClearDirtyFlag() {}
};

namespace nlohmann::detail {
enum class value_t : uint8_t;
}

namespace ImGui {
ImU32 ContentColorByJsonType(nlohmann::detail::value_t type);
bool  SingleLineJsonEdit(char const* str_id, nlohmann::json& value, string const& cacheStr, bool bConfirmEnter = true);
}  // namespace ImGui
