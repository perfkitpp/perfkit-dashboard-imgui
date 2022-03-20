//
// Created by ki608 on 2022-03-18.
//

#include "ConfigWindow.hpp"

#include <perfkit/common/macros.hxx>
#include <perfkit/common/refl/msgpack-rpc/context.hxx>
#include <perfkit/common/utility/cleanup.hxx>

#include "imgui_extension.h"

widgets::ConfigWindow::EditContext widgets::ConfigWindow::globalEditContext;

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
    /// Render editor context
    tryRenderEditorContext();

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
            if (ImGui::InputTextWithHint("##FilterLabel", "Filter", _filterContentBuf, sizeof _filterContentBuf))
            {
                gEvtThisFrame.bHasFilterUpdate = true;
                gEvtThisFrame.filterContent = _filterContentBuf;
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

    // Handle events of this frame using event context.

    /// Render config tree recursively
    // This window is rendered as single category of global config window, managed as header.
    bool bSessionAlive = not _host->SessionAnchor().expired();
    auto wndName = usprintf("%s###%s.CFGWND", _host->DisplayString().c_str(), _host->KeyString().c_str());

    if (not bSessionAlive) { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().DisabledAlpha); }
    if (gEvtThisFrame.bCollapseAll) { ImGui::SetNextItemOpen(false); }
    if (gEvtThisFrame.bExpandAll) { ImGui::SetNextItemOpen(true); }
    bool bRenderComponents = ImGui::CollapsingHeader(wndName, bKeepOpen);
    ImGui::PopStyleVar(not bSessionAlive);

    if (bSessionAlive)
    {
        if (bRenderComponents)
        {
            if (CPPH_TMPVAR = ImGui::ScopedChildWindow(usprintf("%s.REGION", _host->KeyString().c_str())))
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
}

void widgets::ConfigWindow::tryRenderEditorContext()
{
    auto& _ctx = globalEditContext;

    if (std::exchange(_ctx._frameCountFence, gFrameIndex) == gFrameIndex) { return; }
    if (_ctx.ownerRef.lock().get() != this) { return; }

    /// Render editor window
    CPPH_CALL_ON_EXIT(ImGui::End());
    bool bWndKeepOpen = true;
    bool bContinue = ImGui::Begin("config editor", &bWndKeepOpen);

    if (not bWndKeepOpen) { _ctx.ownerRef = {}; }
    if (not bContinue) { return; }
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
            elem->_bHasReceivedUpdate = true;
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
        data->_bHasReceivedUpdate = true;

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
    auto self = &rg.categoryContexts.at(category);

    if (gEvtThisFrame.bHasFilterUpdate)
    {
        // TODO: Calculate filtering chars, and update bFilterSelf
        //  If this node hits filter, propagate result to its parent by parent recursion
    }

    if (gEvtThisFrame.bExpandAll) { self->bBaseOpen = true; }
    if (gEvtThisFrame.bCollapseAll) { self->bBaseOpen = false; }

    bool const bShouldOpen = (self->bBaseOpen || self->bFilterHitChild);
    bool       bTreeIsOpen = false;

    if (not bCollapsed)
    {
        ImGui::SetNextItemOpen(bShouldOpen);
        bTreeIsOpen = ImGui::TreeNodeEx(category->name.c_str(), ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_SpanAvailWidth);

        if (ImGui::IsItemToggledOpen()) { self->bBaseOpen = not self->bBaseOpen; }
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

            ImGui::GetWindowDrawList()->AddRectFilled(
                    ImGui::GetCursorScreenPos(),
                    ImGui::GetCursorScreenPos() + ImVec2{ImGui::GetContentRegionMax().x, ImGui::GetFrameHeight()},
                    ImGui::GetColorU32(ImVec4{.1, .3, .1, std::max<float>(0., .8 - 5. * entity->_timeSinceUpdate.elapsed().count())}));

            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text) - 0x55000000);
            ImGui::TreeNodeEx(
                    usprintf("%s##%p", entity->name.c_str(), entity),
                    ImGuiTreeNodeFlags_Leaf
                            | ImGuiTreeNodeFlags_FramePadding
                            | ImGuiTreeNodeFlags_NoTreePushOnOpen
                            | ImGuiTreeNodeFlags_SpanAvailWidth
                            | ImGuiTreeNodeFlags_AllowItemOverlap);
            ImGui::PopStyleColor();
            bool bOpenEditor = ImGui::IsItemClicked();

            if (exchange(entity->_bHasReceivedUpdate, false))
            {
                entity->_cachedStringify = entity->value.dump();
            }

            if (entity->value.is_boolean() || entity->value.is_number())
            {
                ImGui::SameLine();
                if (ImGui::SingleLineJsonEdit(usprintf("##%p", entity), entity->value, entity->_cachedStringify))
                {
                    NotifyToast{"Update"}.String("Value: {}", entity->value.dump());

                    config_entity_update_t update;
                    update.config_key = entity->configKey;
                    Json::to_msgpack(entity->value, nlohmann::detail::output_adapter<char>(update.content_next));

                    service::update_config_entity(*_host->RpcContext()).notify_one(update);
                }
            }
            else
            {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ContentColorByJsonType(entity->value));
                ImGui::TextUnformatted(entity->_cachedStringify.c_str());
                ImGui::PopStyleColor();
            }
        }

        ImGui::TreePop();
    }
}
