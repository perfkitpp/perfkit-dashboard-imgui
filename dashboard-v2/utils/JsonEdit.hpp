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
        bool bIsDirty : 1;
        bool bIsFocused : 1;

        bool bEvtLostFocus : 1;
        bool bEvtCtrlS : 1;
    };

   private:
    using Json = nlohmann::json;

   private:
    struct Impl;

    unique_ptr<Impl> _self;
    EditorStateFlag _flags = {};

   public:
    JsonEditor();
    ~JsonEditor();

    void Reset(Json&& object, Json const* min = {}, Json const* max = {});
    void Render(void* id);

    auto FlagsThisFrame() const -> EditorStateFlag { return _flags; }
    void ClearDirtyFlag() { _flags = {}; }

    void RetrieveEditing(Json*);
    bool RawEditMode(bool const* bEnable = nullptr);

   private:
    void renderRecurse(Json* ptr, Json const* min, Json const* max);
};

namespace nlohmann::detail {
enum class value_t : uint8_t;
}

namespace ImGui {
ImU32 ContentColorByJsonType(nlohmann::detail::value_t type);
bool SingleLineJsonEdit(char const* str_id, nlohmann::json& value,
                        string const& cacheStr, bool* bIsClicked);
}  // namespace ImGui
