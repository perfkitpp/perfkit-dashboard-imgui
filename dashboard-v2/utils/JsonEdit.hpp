//
// Created by ki608 on 2022-03-20.
//

#pragma once
#include <nlohmann/json_fwd.hpp>

#include "imgui.h"

class JsonEdit
{
};

namespace nlohmann::detail {
enum class value_t : uint8_t;
}

namespace ImGui {
ImU32 ContentColorByJsonType(nlohmann::detail::value_t type);
bool  SingleLineJsonEdit(char const* str_id, nlohmann::json& value, string const& cacheStr, bool bConfirmEnter = true);
}  // namespace ImGui