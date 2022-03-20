//
// Created by ki608 on 2022-03-18.
//

#include "ConfigWindow.hpp"

#include <perfkit/common/macros.hxx>
#include <perfkit/common/utility/cleanup.hxx>

#include "imgui_extension.h"
widgets::ConfigWindow::EditContext widgets::ConfigWindow::globalEditContext;

//
void widgets::ConfigWindow::RenderConfigWindow(bool* bKeepOpen)
{
    /// Since
    static struct
    {
        bool bHasFilterUpdate = false;

    } eventContext;

    /// Render tools -> expand all, collapse all, filter, etc ...
    static size_t _latestFrameCount = 0;
    bool const    bShouldRenderStaticComponents = std::exchange(_latestFrameCount, gFrameIndex) != gFrameIndex;

    if (bShouldRenderStaticComponents)
    {
        // 1. Render toolbars

        // 2.
    }

    // Handle events of this frame using event context.

    /// If editor context is current, render it as child window, which takes upper half area
    ///  of configuration window.

    /// Render config tree recursively
    // This window is rendered as single category of global config window, managed as header.
    bool bSessionAlive = not _host->SessionAnchor().expired();
    auto wndName = usprintf("%s###%s.CFGWND", _host->DisplayString().c_str(), _host->KeyString().c_str());

    if (not bSessionAlive) { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().DisabledAlpha); }
    bool bRenderComponents = ImGui::CollapsingHeader(wndName, bKeepOpen);
    ImGui::PopStyleVar(not bSessionAlive);

    if (bRenderComponents && bSessionAlive)
    {
        if (CPPH_TMPVAR = ImGui::ScopedChildWindow(usprintf("%s.REGION", _host->KeyString().c_str())))
        {
        }
    }

    if (bShouldRenderStaticComponents)
    {
        /// Check for keyboard input, and perform text search on text change.
        /// ESCAPE clears filter buffer.

        /// Render text filter layer if filtering text exists. Its position is fixed-top-right
    }
}

void widgets::ConfigWindow::Tick()
{
    tryRenderEditorContext();
}

void widgets::ConfigWindow::tryRenderEditorContext()
{
    auto& _ctx = globalEditContext;

    if (_ctx.ownerRef.lock().get() != this) { return; }
    if (std::exchange(_ctx._frameCountFence, gFrameIndex) == gFrameIndex) { return; }

    /// Render editor window
    CPPH_CALL_ON_EXIT(ImGui::End());
    bool bWndKeepOpen = true;
    bool bContinue = ImGui::Begin("config editor", &bWndKeepOpen);

    if (not bWndKeepOpen) { _ctx.ownerRef = {}; }
    if (not bContinue) { return; }
}

void widgets::ConfigWindow::_handleNewConfigClassMainThread(
        string key, notify::config_category_t rootCategory)
{
    auto [iter, bIsNew] = _ctxs.try_emplace(std::move(key));
    if (not bIsNew)
        _cleanupRegistryContext(iter->second);

    auto* rg = &iter->second;
    rg->rootCategoryDesc = std::move(rootCategory);

    try
    {
        _recursiveConstructCategories(rg, rg->rootCategoryDesc);
    }
    catch (json::parse_error& ec)
    {
        NotifyToast{"Json Parse Error"}.Error().String(ec.what());
        _cleanupRegistryContext(iter->second);

        _ctxs.erase(iter);
    }
}

void widgets::ConfigWindow::_recursiveConstructCategories(
        ConfigRegistryContext* rg, notify::config_category_t const& desc)
{
    rg->categoryContexts.try_emplace(&desc);

    for (auto& entity : desc.entities)
    {
        auto [iter, bIsNew] = _allEntities.try_emplace(entity.config_key);
        assert(bIsNew);

        rg->entityKeys.push_back(entity.config_key);

        auto* data = &iter->second;
        data->configKey = entity.config_key;
        data->name = entity.name;
        data->description = entity.description;

        if (not entity.initial_value.empty())
            data->value = json::from_msgpack(entity.initial_value);

        if (not entity.opt_max.empty())
            data->optMax = json::from_msgpack(entity.opt_max);
        if (not entity.opt_min.empty())
            data->optMin = json::from_msgpack(entity.opt_min);
        if (not entity.opt_one_of.empty())
            data->optOneOf = json::from_msgpack(entity.opt_one_of);
    }

    for (auto& subcategory : desc.subcategories)
        _recursiveConstructCategories(rg, subcategory);
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
