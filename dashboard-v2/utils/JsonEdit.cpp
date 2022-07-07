//
// Created by ki608 on 2022-03-20.
//

#include "JsonEdit.hpp"

#include <cpph/algorithm/std.hxx>
#include <cpph/helper/macros.hxx>
#include <cpph/utility/cleanup.hxx>
#include <cpph/utility/counter.hxx>
#include <nlohmann/json.hpp>

#include "TextEditor.h"
#include "imgui.h"
#include "imgui_extension.h"
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
            return ColorRefs::GlyphNumber;

        default:
            return ColorRefs::FrontError;
    }
}

bool ImGui::SingleLineJsonEdit(
        char const* str_id, nlohmann::json& value,
        const string& cacheStr, bool* bIsClicked)
{
    bool bValueChanged = false;
    if (value.is_boolean())
        return ImGui::Checkbox(str_id, value.get_ptr<bool*>());

    auto type = value.type();
    auto typeColor = ImGui::ContentColorByJsonType(type);
    ImGui::PushID(&value);
    ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::GetColorU32(ImGuiCol_FrameBg) - 0xbb000000);

    if (value.is_number())
    {
        ImGui::AlignTextToFramePadding();

        // Edit number box
        void* vdata = nullptr;
        ImGuiDataType dataType = -1;

        switch (value.type())
        {
            case nlohmann::detail::value_t::number_integer:
            case nlohmann::detail::value_t::number_unsigned:
                vdata = value.get_ptr<int64_t*>();
                dataType = ImGuiDataType_S64;
                break;

            case nlohmann::detail::value_t::number_float:
                vdata = value.get_ptr<double*>();
                dataType = ImGuiDataType_Double;
                break;

            default: abort();
        }

        ImGui::SetNextItemWidth(-1.f);
        if (ImGui::InputScalar("##Scalar", dataType, vdata, 0, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            ImGui::SetKeyboardFocusHere(-1);
            bValueChanged = true;
        }
    }
    else if (value.is_string())
    {
        auto& str = value.get_ref<string&>();

        ImGui::SetNextItemWidth(-1.f);

        if (ImGui::InputText("##TEDIT", str, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            ImGui::SetKeyboardFocusHere(-1);
            bValueChanged = true;
        }
    }
    else
    {
        *bIsClicked = ImGui::Selectable("##SEL");
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(cacheStr.data(), cacheStr.data() + cacheStr.size());
    }

    ImGui::PopStyleColor(2);
    ImGui::PopID();

    return bValueChanged;
}

struct JsonEditor::Impl
{
    Json editing;
    optional<Json> min;
    optional<Json> max;

    bool bRawEditMode = false;
    TextEditor rawEditor;

    TextEditor stringEditor;
    void* editingID = nullptr;
};

void JsonEditor::Render(void* id, float heightOverride)
{
    ImGui::PushID(id);
    CPPH_FINALLY(ImGui::PopID());

    if (_self->bRawEditMode)
    {
        if (heightOverride == -1)
        {
            heightOverride = ImGui::GetTextLineHeight() * (_self->rawEditor.GetTotalLines() + 1);
            heightOverride = std::min(heightOverride, 400 * DpiScale());
        }

        _self->rawEditor.Render("##EditJson", {0, heightOverride}, false);
        _flags.bIsDirty = _flags.bIsDirty || _self->rawEditor.IsTextChanged();
    }
    else
    {
        renderRecurse(
                &_self->editing,
                _self->min ? &*_self->min : nullptr,
                _self->max ? &*_self->max : nullptr);
    }
}

void JsonEditor::Reset(JsonEditor::Json&& object, const JsonEditor::Json* min, const JsonEditor::Json* max)
{
    auto& s = *_self;
    s.editing = std::move(object);
    if (_self->bRawEditMode) { _self->rawEditor.SetText(s.editing.dump(2)); }

    s.min.reset();
    s.max.reset();

    if (min) { s.min.emplace(*min); }
    if (max) { s.max.emplace(*max); }

    _flags = {};
}

JsonEditor::JsonEditor()
        : _self(make_unique<Impl>())
{
}

void JsonEditor::renderRecurse(JsonEditor::Json* ptr, Json const* min, Json const* max)
{
    if (min && ptr->type() != min->type()) { min = {}; }
    if (max && ptr->type() != max->type()) { max = {}; }

    auto type = ptr->type();
    auto typeColor = ImGui::ContentColorByJsonType(type);

    ImGui::PushID(ptr);
    CPPH_FINALLY(ImGui::PopID());

    switch (type)
    {
        case nlohmann::detail::value_t::null:
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(
                    ImGui::ColorConvertU32ToFloat4(typeColor),
                    "null");
            break;
        }

        case nlohmann::detail::value_t::object:
        {
            ImGui::Text("{");
            ImGui::TreePush();

            size_t n = 0;
            for (auto& [k, v] : ptr->items())
            {
                // TODO: Make key configurable
                // TODO: Add cloneable feature

                ImGui::PushStyleColor(ImGuiCol_Text, ColorRefs::GlyphKeyword);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(k.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 0), ImGui::TextUnformatted(":");
                ImGui::SameLine();

                renderRecurse(
                        &v,
                        min ? perfkit::find_ptr(*min, k) : nullptr,
                        max ? perfkit::find_ptr(*max, k) : nullptr);

                if (++n < ptr->size())
                {
                    ImGui::SameLine(0, 0), ImGui::Text(",");
                }
            }
            ImGui::TreePop();

            if (ptr->empty()) { ImGui::SameLine(0, 0); }
            ImGui::Text("}");
            break;
        }

        case nlohmann::detail::value_t::array:
        {
            ImGui::Text("[");
            ImGui::TreePush();

            for (int n : perfkit::counter(ptr->size()))
            {
                // TODO: Add cloneable feature

                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(usprintf("[%zu]", n));

                ImGui::SameLine();
                renderRecurse(
                        &(*ptr)[n],
                        min && n < min->size() ? &(*min)[n] : nullptr,
                        max && n < max->size() ? &(*max)[n] : nullptr);

                if (n + 1 < ptr->size())
                {
                    ImGui::SameLine(0, 0), ImGui::Text(",");
                }
            }
            ImGui::TreePop();

            if (ptr->empty()) { ImGui::SameLine(0, 0); }
            ImGui::Text("]");
            break;
        }

        case nlohmann::detail::value_t::string:
        {
            ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
            ImGui::AlignTextToFramePadding();

            // Edit string box
            auto constexpr POPUP_ID = "##OpenTextEdit";

            if (ImGui::IsPopupOpen(POPUP_ID))
            {
                ImGui::Text("-- editing --");

                ImGui::SetNextWindowSize(ImVec2{480, 272} * DpiScale());
                if (CondInvoke(ImGui::BeginPopup(POPUP_ID), &ImGui::EndPopup))
                {
                    _self->stringEditor.Render("##Editor");
                    if (_self->stringEditor.IsTextChanged()) { _flags.bIsDirty = true; }

                    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::IsKeyPressed(ImGuiKey_S))
                        ptr->get_ref<string&>() = _self->stringEditor.GetText();
                }
            }
            else
            {
                if (ptr == _self->editingID)
                {  // Editor popup has just been closed.
                    _self->editingID = nullptr;
                    ptr->get_ref<string&>() = _self->stringEditor.GetText();
                    ptr->get_ref<string&>().pop_back();  // Erase last newline
                }

                auto editStr = ImGui::Selectable("##Btn");
                auto strRef = &ptr->get_ref<string&>();
                ImGui::SameLine();
                ImGui::TextUnformatted(strRef->c_str());

                if (editStr)
                {
                    ImGui::OpenPopup(POPUP_ID);
                    _self->stringEditor.SetText(*strRef);
                    _self->editingID = ptr;
                }
            }

            ImGui::PopStyleColor();
            break;
        }

        case nlohmann::detail::value_t::boolean:
        {
            if (ImGui::Checkbox("##Check", ptr->get_ptr<bool*>()))
            {
                _flags.bIsDirty = true;
            }
            break;
        }

        case nlohmann::detail::value_t::number_integer:
        case nlohmann::detail::value_t::number_unsigned:
        case nlohmann::detail::value_t::number_float:
        {
            ImGui::PushStyleColor(ImGuiCol_Text, typeColor);
            ImGui::AlignTextToFramePadding();

            // Edit number box
            void* vdata = nullptr;
            void const *vmin = nullptr, *vmax = nullptr;
            ImGuiDataType dataType = -1;
            float step = 1.f;

            switch (ptr->type())
            {
                case nlohmann::detail::value_t::number_integer:
                case nlohmann::detail::value_t::number_unsigned:
                    vdata = ptr->get_ptr<int64_t*>();
                    if (min) { vmin = min->get_ptr<int64_t const*>(); }
                    if (max) { vmax = max->get_ptr<int64_t const*>(); }
                    dataType = ImGuiDataType_S64;
                    step = std::max(0.005, std::abs(*ptr->get_ptr<int64_t*>() * 0.0005)) / DpiScale();
                    break;

                case nlohmann::detail::value_t::number_float:
                    vdata = ptr->get_ptr<double*>();
                    if (min) { vmin = min->get_ptr<double const*>(); }
                    if (max) { vmax = max->get_ptr<double const*>(); }
                    dataType = ImGuiDataType_Double;
                    step = std::max(1e-6, std::abs(*ptr->get_ptr<double*>() * 0.0005)) / DpiScale();
                    break;

                default: abort();
            }

            ImGui::SetNextItemWidth(-1.f);
            if (vmin && vmax)
            {  // Create slider control
                if (ImGui::SliderScalar("##Scalar", dataType, vdata, vmin, vmax))
                    _flags.bIsDirty = true;
            }
            else
            {  // Create drag control
                if (ImGui::DragScalar("##Scalar", dataType, vdata, step, vmin, vmax))
                    _flags.bIsDirty = true;
            }

            ImGui::PopStyleColor();
            break;
        }

        case nlohmann::detail::value_t::binary:
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("--binary--");
            break;
        }

        case nlohmann::detail::value_t::discarded:
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("--error--");
            break;
        }
    }
}

void JsonEditor::RetrieveEditing(Json* out)
{
    if (_self->bRawEditMode)
    {
        *out = Json::parse(_self->rawEditor.GetText(), nullptr, false);

        // Successfully parsed editing content.
        if (not out->is_discarded())
        {
            _self->editing = *out;
            return;
        }
        else
        {
            NotifyToast{"JSON: Raw editor content parsing failed"}
                    .Error()
                    .String("Cached json content will be used.");
        }
    }

    // Copy currently editing content.
    *out = _self->editing;
}

bool JsonEditor::RawEditMode(bool const* bEnable)
{
    if (bEnable)
    {
        auto bPrev = exchange(_self->bRawEditMode, *bEnable);
        if (bPrev != _self->bRawEditMode)
        {
            if (_self->bRawEditMode)
            {
                // Switched to raw editing mode
                _self->rawEditor.SetText(_self->editing.dump(2));
            }
            else
            {
                // Switched to normal editing mode
                auto json = Json::parse(_self->rawEditor.GetText(), nullptr, false);
                if (not json.is_discarded())
                {
                    _self->editing = json;
                }
                else
                {
                    NotifyToast{"JSON: Raw editor content parsing failed"}
                            .Error()
                            .String("Cached json content will be used.");
                }
            }
        }
    }
    return _self->bRawEditMode;
}

JsonEditor::~JsonEditor() = default;
