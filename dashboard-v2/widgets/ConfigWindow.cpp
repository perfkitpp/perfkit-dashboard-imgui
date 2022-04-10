//
// Created by ki608 on 2022-03-18.
//

#include "ConfigWindow.hpp"

#include <perfkit/common/macros.hxx>
#include <perfkit/common/refl/object.hxx>
#include <perfkit/common/refl/rpc/rpc.hxx>
#include <perfkit/common/utility/cleanup.hxx>

#include "imgui_extension.h"

widgets::ConfigWindow::EditContext widgets::ConfigWindow::globalEditContext;
static bool                        bFilterTargetDirty = false;

static struct
{
    bool        bShouldApplyFilter = false;
    bool        bHasFilterUpdate = false;

    string_view filterContent = {};

    bool        bExpandAll = false;
    bool        bCollapseAll = false;
} gEvtThisFrame;

//
void widgets::ConfigWindow::RenderConfigWindow(bool* bKeepOpen)
{
    /// Render tools -> expand all, collapse all, filter, etc ...
    static size_t _latestFrameCount = 0;
    bool const    bShouldRenderStaticComponents = std::exchange(_latestFrameCount, gFrameIndex) != gFrameIndex;

    if (bShouldRenderStaticComponents)
    {
        gEvtThisFrame = {};

        /// Render toolbar
        if (CondInvoke(ImGui::BeginMenuBar(), ImGui::EndMenuBar))
        {
            static char _filterContentBuf[256];

            /// 'Search' mini window
            /// Check for keyboard input, and perform text search on text change.
            /// ESCAPE clears filter buffer.
            ImGui::SetNextItemWidth(-60);
            if (ImGui::InputTextWithHint("##FilterLabel", "Filter", _filterContentBuf, sizeof _filterContentBuf)
                || exchange(bFilterTargetDirty, false))
            {
                gEvtThisFrame.bHasFilterUpdate = true;
                gEvtThisFrame.filterContent = _filterContentBuf;
                for (auto i = 0; i < gEvtThisFrame.filterContent.size(); ++i) { _filterContentBuf[i] = tolower(_filterContentBuf[i]); }
            }

            if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                _filterContentBuf[0] = 0;
                ImGui::SetKeyboardFocusHere(-1);
                gEvtThisFrame.bHasFilterUpdate = false;
            }

            gEvtThisFrame.bShouldApplyFilter = strlen(_filterContentBuf);

            /// Minimize/Maximize button
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 34);
            gEvtThisFrame.bExpandAll = ImGui::SmallButton("+");
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 18);
            gEvtThisFrame.bCollapseAll = ImGui::SmallButton("=");
        }
    }

    /// Render config tree recursively
    // This window is rendered as single category of global config window, managed as header.
    bool bSessionAlive = not _host->SessionAnchor().expired();
    auto wndName = usprintf("%s###%s.CFGWND", _host->DisplayString().c_str(), _host->KeyString().c_str());

    if (not bSessionAlive) { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().DisabledAlpha); }
    if (gEvtThisFrame.bCollapseAll) { ImGui::SetNextItemOpen(false); }
    if (gEvtThisFrame.bExpandAll) { ImGui::SetNextItemOpen(true); }

    bool bRenderComponents = ImGui::CollapsingHeader(wndName, bKeepOpen);
    if (ImGui::IsItemToggledOpen())
        bFilterTargetDirty = true;

    ImGui::PopStyleVar(not bSessionAlive);

    if (bSessionAlive)
    {
        if (bRenderComponents)
        {
            CPPH_TMPVAR = ImGui::ScopedChildWindow(usprintf("%s.REGION", _host->KeyString().c_str()));

            for (auto& [key, ctx] : _ctxs)
                recursiveTickSubcategory(ctx, &ctx.rootCategoryDesc, not bRenderComponents);
        }
        else
        {
            for (auto& [key, ctx] : _ctxs)
                recursiveTickSubcategory(ctx, &ctx.rootCategoryDesc, not bRenderComponents);
        }
    }
}

void widgets::ConfigWindow::Tick()
{
    /// Render editor context
    tryRenderEditorContext();
}

void widgets::ConfigWindow::tryRenderEditorContext()
{
    auto& _ctx = globalEditContext;

    if (_ctx.ownerRef.lock().get() != this) { return; }
    if (std::exchange(_ctx._frameCountFence, gFrameIndex) == gFrameIndex) { return; }

    auto entity = &*_ctx.entityRef.lock();

    /// Render editor window
    CPPH_CALL_ON_EXIT(ImGui::End());
    bool bWndKeepOpen = true;
    bool bContinue = ImGui::Begin(usprintf("edit: %s###CONFEDIT", entity->name.c_str()), &bWndKeepOpen, ImGuiWindowFlags_MenuBar);

    if (not bWndKeepOpen) { _ctx.ownerRef = {}, _ctx._editingRef = {}; }
    if (not bWndKeepOpen || not bContinue) { return; }

    if (exchange(_ctx._editingRef, entity) != entity)
    {
        // Editing target has changed ...
        _ctx.editor.Reset(Json{entity->value});
        entity->_bHasUpdateForEditor = false;
    }

    if (CondInvoke(ImGui::BeginMenuBar(), ImGui::EndMenuBar))
    {
        ImGui::Checkbox("Update on edit", &entity->_bUpdateOnEdit);
        ImGui::Checkbox("Edit in raw", &entity->_bEditInRaw);

        ImGui::PushStyleColor(ImGuiCol_Text, ColorRefs::FrontWarn);
        if (entity->_bHasUpdateForEditor && ImGui::SmallButton("Reload!"))
        {
            _ctx.editor.Reset(Json{entity->value});
            entity->_bHasUpdateForEditor = false;
        }
        ImGui::PopStyleColor();
    }

    // TODO: Edit json content
}

void widgets::ConfigWindow::_handleNewConfigClassMainThread(
        uint64_t id, string key, CategoryDesc rootCategory)
{
    auto [iter, bIsNew] = _ctxs.try_emplace(std::move(key));
    if (not bIsNew)
    {
        // Skip entities which are simply republished
        if (iter->second.id == id)
            return;

        _cleanupRegistryContext(iter->second);
    }

    auto* rg = &iter->second;
    rg->id = id;
    rg->rootCategoryDesc = std::move(rootCategory);

    try
    {
        _recursiveConstructCategories(rg, rg->rootCategoryDesc, nullptr);
    }
    catch (Json::parse_error& ec)
    {
        NotifyToast{"Json Parse Error"}.Error().String(ec.what());
        _cleanupRegistryContext(iter->second);

        _ctxs.erase(iter);
    }

    // Refresh filter if being applied
    bFilterTargetDirty = true;
}

void widgets::ConfigWindow::_handleConfigsUpdate(config_entity_update_t const& entityDesc)
{
    if (auto* pair = perfkit::find_ptr(_allEntities, entityDesc.config_key))
    {
        auto elem = &pair->second;
        auto parsed = Json::from_msgpack(entityDesc.content_next, true, false);
        if (not parsed.is_discarded())
        {
            elem->value = move(parsed);
            elem->_bHasUpdate = true;
            elem->_timeSinceUpdate.reset();
        }
        else
        {
            NotifyToast{"System Error"}.Error().String("Unkown config key!");
        }
    }
    else
    {
        NotifyToast{"System Error"}.Error().String("Unkown config key!");
        return;
    }
}

void widgets::ConfigWindow::_recursiveConstructCategories(
        ConfigRegistryContext* rg,
        CategoryDesc const&    desc,
        ConfigCategoryContext* parent)
{
    auto category = &rg->categoryContexts[&desc];
    category->selfRef = &desc;
    category->parentContext = parent;

    for (auto& entity : desc.entities)
    {
        auto [iter, bIsNew] = _allEntities.try_emplace(entity.config_key);
        assert(bIsNew);

        rg->entityKeys.push_back(entity.config_key);

        auto* data = &iter->second;
        data->configKey = entity.config_key;
        data->name = entity.name;
        data->description = entity.description;
        data->_bHasUpdate = true;

        if (not entity.initial_value.empty())
            data->value = Json::from_msgpack(entity.initial_value);

        if (not entity.opt_max.empty())
            data->optMax = Json::from_msgpack(entity.opt_max);
        if (not entity.opt_min.empty())
            data->optMin = Json::from_msgpack(entity.opt_min);
        if (not entity.opt_one_of.empty())
            data->optOneOf = Json::from_msgpack(entity.opt_one_of);
    }

    for (auto& subcategory : desc.subcategories)
        _recursiveConstructCategories(rg, subcategory, category);
}

void widgets::ConfigWindow::_cleanupRegistryContext(ConfigRegistryContext& rg)
{
    // 1. Unregister all entity contexts
    for (auto key : rg.entityKeys)
        _allEntities.erase(key);

    // 2. Cleanup registry contents
    rg = {};
}

void widgets::ConfigWindow::ClearContexts()
{
    for (auto& [name, ctx] : _ctxs)
        _cleanupRegistryContext(ctx);

    _ctxs.clear();
}

void widgets::ConfigWindow::recursiveTickSubcategory(
        ConfigRegistryContext& rg,
        CategoryDescPtr        category,
        bool                   bCollapsed)
{
    auto        self = &rg.categoryContexts.at(category);
    auto const& evt = gEvtThisFrame;

    auto const  fnCheckFilter
            = ([](string_view key) -> optional<pair<int, int>> {
                  static string buf1;

                  buf1.resize(key.size());
                  transform(key, buf1.begin(), tolower);

                  auto const& evt = gEvtThisFrame;

                  auto        pos = buf1.find(evt.filterContent);
                  if (pos == key.npos)
                      return {};
                  else
                      return make_pair(int(pos), int(pos + evt.filterContent.size()));
              });

    auto const fnPropagateFilterHit
            = perfkit::y_combinator{[this](auto This, ConfigCategoryContext* selfPtr) -> void {
                  if (not selfPtr) { return; }

                  // If bFilterHitChild is already true, it indicates there is another
                  //  filter hit already.
                  if (exchange(selfPtr->bFilterHitChild, true)) { return; }

                  // Recursively set flag
                  This(selfPtr->parentContext);
              }};

    auto const fnRenderFilteredLabel
            = [](string_view text, FilterEntity const& entity, uint32_t baseColor = ImGui::GetColorU32(ImGuiCol_Text)) {
                  ImGui::PushStyleColor(ImGuiCol_Text, baseColor);
                  CPPH_CALL_ON_EXIT(ImGui::PopStyleColor());

                  ImGui::Spacing();
                  ImGui::SameLine();

                  if (not gEvtThisFrame.bShouldApplyFilter || not entity.bFilterHitSelf)
                  {
                      ImGui::TextUnformatted(text.data(), text.data() + text.size());
                  }
                  else
                  {
                      auto strBeg = text.data();
                      auto strFltBeg = text.data() + entity.FilterCharsRange.first;
                      auto strFltEnd = text.data() + entity.FilterCharsRange.second;
                      auto strEnd = text.data() + text.size();
                      IM_ASSERT(strFltEnd <= strEnd);

                      ImGui::TextUnformatted(strBeg, strFltBeg);
                      ImGui::SameLine(0, 0), ImGui::PushStyleColor(ImGuiCol_Text, 0xffffff00);
                      ImGui::TextUnformatted(strFltBeg, strFltEnd);
                      ImGui::SameLine(0, 0), ImGui::PopStyleColor();
                      ImGui::TextUnformatted(strFltEnd, strEnd);
                  }
              };

    if (evt.bHasFilterUpdate)
    {
        // Calculate filtering chars, and update bFilterSelf
        //  If this node hits filter, propagate result to its parent by parent recursion
        self->bFilterHitChild = false;
        self->bFilterHitSelf = false;

        auto hitResult = fnCheckFilter(category->name);
        if (hitResult)
        {
            self->bFilterHitSelf = true;
            self->FilterCharsRange = *hitResult;
            fnPropagateFilterHit(self);
            self->bFilterHitChild = false;
        }

        for (auto& entityDesc : category->entities)
        {
            auto entity = &_allEntities.at(entityDesc.config_key);

            // Check for filter
            if (auto hitRes = fnCheckFilter(entity->name))
            {
                entity->bFilterHitSelf = true;
                entity->FilterCharsRange = *hitRes;
                fnPropagateFilterHit(self);
            }
            else
            {
                entity->bFilterHitSelf = false;
            }
        }
    }

    if (evt.bExpandAll) { self->bBaseOpen = true; }
    if (evt.bCollapseAll) { self->bBaseOpen = false; }

    bCollapsed = bCollapsed || evt.bShouldApplyFilter && not self->bFilterHitChild;
    bool const bShouldOpen = (self->bBaseOpen || evt.bShouldApplyFilter && self->bFilterHitChild) && not bCollapsed;
    bool       bTreeIsOpen = false;

    if (not bCollapsed)
    {
        ImGui::SetNextItemOpen(bShouldOpen);
        bTreeIsOpen = ImGui::TreeNodeEx(usprintf("##%p", category), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanFullWidth);
        if (ImGui::IsItemToggledOpen()) { self->bBaseOpen = not self->bBaseOpen; }

        ImGui::SameLine(0, 0);
        fnRenderFilteredLabel(category->name, *self);
    }

    for (auto& subCategory : category->subcategories)
    {
        recursiveTickSubcategory(rg, &subCategory, not bTreeIsOpen);
    }

    if (bTreeIsOpen)
    {
        for (auto& entityDesc : category->entities)
        {
            auto entity = &_allEntities.at(entityDesc.config_key);
            if (evt.bShouldApplyFilter && not entity->bFilterHitSelf) { continue; }

            // Draw update highlight for short time after receiving update
            if (auto alphaValue = std::max<float>(0., .8 - 5. * entity->_timeSinceUpdate.elapsed().count()))
                ImGui::GetWindowDrawList()->AddRectFilled(
                        {ImGui::GetCursorScreenPos().x - ImGui::GetCursorPosX(), ImGui::GetCursorScreenPos().y},
                        ImGui::GetCursorScreenPos() + ImVec2{ImGui::GetContentRegionMax().x, ImGui::GetFrameHeight()},
                        ImGui::GetColorU32(ImVec4{.1, .3, .1, alphaValue}));

            auto labelColor
                    = entity->_bIsDirty                       ? ColorRefs::FrontWarn - 0xee000000
                    : globalEditContext._editingRef == entity ? 0x11ffffff
                                                              : 0;

            if (labelColor & 0xff000000)
                ImGui::GetWindowDrawList()->AddRectFilled(
                        {ImGui::GetCursorScreenPos().x - ImGui::GetCursorPosX(), ImGui::GetCursorScreenPos().y},
                        ImGui::GetCursorScreenPos() + ImVec2{ImGui::GetContentRegionMax().x, ImGui::GetFrameHeight()},
                        labelColor);

            ImGui::TreeNodeEx(
                    usprintf("##%p.TreeNode", entity),
                    ImGuiTreeNodeFlags_Leaf
                            | ImGuiTreeNodeFlags_FramePadding
                            | ImGuiTreeNodeFlags_NoTreePushOnOpen
                            | ImGuiTreeNodeFlags_SpanFullWidth
                            | ImGuiTreeNodeFlags_AllowItemOverlap);

            bool const bIsItemClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
            bool const bIsItemHovered = ImGui::IsItemHovered();

            if (bIsItemClicked)
            {
                globalEditContext.ownerRef = rg.WrapPtr(this);
                globalEditContext.entityRef = rg.WrapPtr(entity);
            }

            if (bIsItemHovered)
            {
                ImGui::PushTextWrapPos(0);
                CPPH_CALL_ON_EXIT(ImGui::PopTextWrapPos());

                ImGui::SetNextWindowSize({240, 0});
                ImGui::BeginTooltip();
                CPPH_CALL_ON_EXIT(ImGui::EndTooltip());

                ImGui::TextUnformatted(entity->name.c_str());
                ImGui::Separator();

                ImGui::PushStyleColor(ImGuiCol_Text, 0xff888888);
                ImGui::TextWrapped("%s", entity->description.empty() ? "--no description--" : entity->description.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SameLine(0, 0);
            fnRenderFilteredLabel(entity->name, *entity, 0xffbbbbbb);

            if (exchange(entity->_bHasUpdate, false))
            {
                entity->_bHasUpdateForEditor = true;
                entity->_cachedStringify = entity->value.dump();
                entity->_bIsDirty = false;
            }

            ImGui::SameLine(0, 0);
            ImGui::TextColored({1, 1, 0, .7}, entity->_bIsDirty ? "*" : " ");
            ImGui::SameLine();

            bool bHasUpdate = false;

            if (not entity->optOneOf.empty() && entity->optOneOf.is_array())
            {
                // TODO: Implement 'OneOf' selector
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ContentColorByJsonType(entity->value));
                CPPH_CALL_ON_EXIT(ImGui::PopStyleColor());

                ImGui::SetNextItemWidth(-1);
                auto bOpenCombo = ImGui::BeginCombo(
                        usprintf("##%pComboSelect", entity),
                        entity->_cachedStringify.c_str());

                if (bOpenCombo)
                {
                    CPPH_CALL_ON_EXIT(ImGui::EndCombo());

                    for (auto& e : entity->optOneOf)
                    {
                        auto str = e.dump();
                        auto bSelected = ImGui::Selectable(str.c_str());

                        if (bSelected)
                        {
                            bHasUpdate = true;
                            entity->value = e;

                            break;
                        }
                    }
                }
            }
            else if (entity->value.is_boolean() || entity->value.is_number())
            {
                bHasUpdate = ImGui::SingleLineJsonEdit(
                        usprintf("##%p", entity),
                        entity->value,
                        entity->_cachedStringify);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ContentColorByJsonType(entity->value));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(entity->_cachedStringify.c_str());
                ImGui::PopStyleColor();
            }

            if (bHasUpdate)
            {
                entity->_bIsDirty = true;

                config_entity_update_t update;
                update.config_key = entity->configKey;
                Json::to_msgpack(entity->value, nlohmann::detail::output_adapter<char>(update.content_next));

                service::update_config_entity(_host->RpcSession()).notify(update);
            }
        }

        ImGui::TreePop();
    }
}
