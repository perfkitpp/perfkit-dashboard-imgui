//
// Created by ki608 on 2022-03-18.
//

#pragma once
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "TextEditor.h"
#include "cpph/memory/pool.hxx"
#include "cpph/utility/timer.hxx"
#include "interfaces/RpcSessionOwner.hpp"
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
    using CategoryDesc = notify::config_category_t;

   public:
    //
    //              Config Type Definitions
    //

    struct FilterEntity {
        //! Indicates this entity contains filter string
        bool bFilterHitSelf = false;

        //! Matching range if filter was hit on this entity.
        pair<int, int> FilterCharsRange = {};
    };

    struct ConfigEntityContext : FilterEntity {
        uint64_t configKey;
        Json value;

        string name;
        string description;

        Json optMin;
        Json optMax;
        Json optOneOf;

        //! [transient]
        string _cachedStringify;

        bool _bIsDirty = false;
        bool _bHasUpdate = false;
        bool _bHasUpdateForEditor = false;
        perfkit::stopwatch _timeSinceUpdate;

        //! [configs]
        bool _bEditInRaw = false;
        bool _bUpdateOnEdit = false;
    };

    struct ConfigCategoryContext : FilterEntity {
        //! Self reference
        CategoryDesc const* selfRef;

        //! Reference to parent
        ConfigCategoryContext* parentContext = nullptr;

        //! Open status from user input. Can be overridden by filtering status
        bool bBaseOpen = false;

        //! Indicates any of child window contains filter string
        bool bFilterHitChild = false;
    };

    struct ConfigRegistryContext {
       private:
        //! Anchor for lifecycle management
        shared_ptr<void> _anchor = make_shared<nullptr_t>();

       public:
        //! Id of this entity. Used for identifying republish/actual dispose.
        uint64_t id;

        //! Entity list is constant after initial build
        vector<uint64_t> entityKeys;

        //! Config category descriptor
        pool_ptr<CategoryDesc const> rootCategoryDesc;

        //! Config contexts
        unordered_map<uint64_t, ConfigCategoryContext> categoryContexts;

       public:
        template <typename Ty_>
        auto WrapPtr(Ty_* ptr)
        {
            return shared_ptr<Ty_>(_anchor, ptr);
        }
    };

    struct EditContext {
        //! Reference to owner. Uses control block of registry context, which releases
        //!  ownership automatically on registry context cleanup.
        weak_ptr<ConfigWindow> ownerRef;

        //! Weak reference to edit target
        weak_ptr<ConfigEntityContext> entityRef;

        //! Cached editor instance
        JsonEditor editor;

        //! Check if editing entity is dirty
        bool bDirty = false;

        //! Editing entity context ... cached for fast comparison
        ConfigEntityContext* _editingRef = {};

        //! [transient]
        size_t _frameCountFence = 0;
        bool _bReloadFrame = false;
    };

   private:
    IRpcSessionOwner* _host;

    //! Recv pool
    pool<CategoryDesc> _poolCatRecv;

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
    void Render(bool* bKeepOpen);
    void ClearContexts();

   private:
    // Try to render editor context of this frame.
    //
    void tryRenderEditorContext();
    void recursiveTickSubcategory(ConfigRegistryContext& rg, CategoryDesc const& category, bool bCollapsed = false);
    void commitEntity(ConfigEntityContext*);

   public:
    //
    //              Event Handling
    //
    void HandleNewConfigClass(uint64_t id, string const& key, CategoryDesc& root)
    {
        auto ptr = _poolCatRecv.checkout();
        swap(root, *ptr);

        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind(&Self::_handleNewConfigClassMainThread, this, id, key, move(ptr)));
    }

    void HandleConfigUpdate(config_entity_update_t const& entity)
    {
        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind(&Self::_handleConfigsUpdate, this, entity));
    }

    void HandleDeletedConfigClass(string const& key)
    {
        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind(&Self::_handleDeletedConfigClass, this, key));
    }

   private:
    void _handleNewConfigClassMainThread(uint64_t, string, pool_ptr<CategoryDesc>&);
    void _handleConfigsUpdate(config_entity_update_t const& entity);
    void _handleDeletedConfigClass(string const& key);

    void _recursiveConstructCategories(ConfigRegistryContext* rg, CategoryDesc const& desc, ConfigCategoryContext* parent);
    void _collectGarbage();
};
}  // namespace widgets
