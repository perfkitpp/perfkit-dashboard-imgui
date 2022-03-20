//
// Created by ki608 on 2022-03-18.
//

#pragma once
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "TextEditor.h"
#include "interfaces/RpcSessionOwner.hpp"
#include "perfkit/common/timer.hxx"
#include "perfkit/extension/net/protocol.hpp"
#include "utils/JsonEdit.hpp"

namespace widgets {
using namespace perfkit;
using namespace perfkit::net::message;

using std::map;
using std::unordered_map;

class ConfigWindow
{
    using Self = ConfigWindow;
    using Json = nlohmann::json;
    using CategoryDescPtr = notify::config_category_t const*;
    using CategoryDesc = notify::config_category_t;

   public:
    //
    //              Config Type Definitions
    //

    struct ConfigEntityContext
    {
        uint64_t configKey;
        Json     value;

        string   name;
        string   description;

        Json     optMin;
        Json     optMax;
        Json     optOneOf;

        //! [transient]
        string             _cachedStringify;

        bool               _bIsDirty = false;
        bool               _bHasReceivedUpdate = false;
        perfkit::stopwatch _timeSinceUpdate;

        bool               _bEditInRaw = false;
        bool               _bUpdateOnEdit = false;
    };

    struct ConfigCategoryContext
    {
        //! Self reference
        CategoryDescPtr selfRef = nullptr;

        //! Reference to parent
        ConfigCategoryContext* parentContext = nullptr;

        //! Open status from user input. Can be overridden by filtering status
        bool bBaseOpen = false;

        //! [transient]
        bool bFilterHitSelf = false;
        bool bFilterHitChild = false;
        int  FilterCharsRange[2] = {};
    };

    struct ConfigRegistryContext
    {
       private:
        //! Anchor for lifecycle management
        shared_ptr<void> _anchor = make_shared<nullptr_t>();

       public:
        //! Id of this entity. Used for identifying republish/actual dispose.
        uint64_t id;

        //! Entity list is constant after initial build
        vector<uint64_t> entityKeys;

        //! Config category descriptor
        CategoryDesc rootCategoryDesc;

        //! Config contexts
        unordered_map<CategoryDescPtr, ConfigCategoryContext> categoryContexts;

       public:
        template <typename Ty_>
        auto WrapPtr(Ty_* ptr)
        {
            return shared_ptr<Ty_>(_anchor, ptr);
        }
    };

    struct EditContext
    {
        //! Reference to owner. Uses control block of registry context, which releases
        //!  ownership automatically on registry context cleanup.
        weak_ptr<ConfigWindow> ownerRef;

        //! Weak reference to edit target
        weak_ptr<ConfigEntityContext> entityRef;

        //! Text editor for various context
        TextEditor editor;

        //! [transient]
        size_t _frameCountFence = 0;
    };

   private:
    IRpcSessionOwner* _host;

    //! List of config registry contexts.
    map<string, ConfigRegistryContext> _ctxs;

    //!
    static EditContext globalEditContext;

    //! All config entities
    unordered_map<uint64_t, ConfigEntityContext> _allEntities;

   public:
    explicit ConfigWindow(IRpcSessionOwner* host) noexcept : _host(host) {}

   public:
    //
    //               Rendering
    //
    void Tick();
    void RenderConfigWindow(bool* bKeepOpen);
    void ClearContexts();

   private:
    // Try to render editor context of this frame.
    //
    void tryRenderEditorContext();
    void recursiveTickSubcategory(ConfigRegistryContext& rg, CategoryDescPtr category, bool bCollapsed = false);

   public:
    //
    //              Event Handling
    //
    void HandleNewConfigClass(uint64_t id, string const& key, CategoryDesc const& root)
    {
        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind_front(&Self::_handleNewConfigClassMainThread, this, id, key, root));
    }

    void HandleConfigUpdate(config_entity_update_t const& entity)
    {
        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind_front(&Self::_handleConfigsUpdate, this, entity));
    }

   private:
    void _handleNewConfigClassMainThread(uint64_t, string, CategoryDesc);
    void _handleConfigsUpdate(config_entity_update_t const& entity);

    void _cleanupRegistryContext(ConfigRegistryContext& rg);
    void _recursiveConstructCategories(ConfigRegistryContext* rg, CategoryDesc const& ref, ConfigCategoryContext* parent);
};
}  // namespace widgets
