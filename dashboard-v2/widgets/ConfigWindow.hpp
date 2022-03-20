//
// Created by ki608 on 2022-03-18.
//

#pragma once
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "TextEditor.h"
#include "interfaces/RpcSessionOwner.hpp"
#include "perfkit/extension/net/protocol.hpp"

namespace widgets {
using namespace perfkit;
using namespace perfkit::net::message;

using std::map;
using std::unordered_map;

class ConfigWindow
{
    using Self = ConfigWindow;
    using json = nlohmann::json;

   public:
    //
    //              Config Type Definitions
    //

    struct ConfigEntityContext
    {
        uint64_t configKey;
        json     value;

        string   name;
        string   description;

        json     optMin;
        json     optMax;
        json     optOneOf;

        //! [transient]
        bool _bIsDirty = false;
        bool _bHasReceivedUpdate = false;

        bool _bEditInRaw = false;
        bool _bUpdateOnEdit = false;
    };

    struct ConfigCategoryContext
    {
        //! Open status from user input. Can be overridden by filtering status
        bool bBaseOpen = false;

        //! [transient]
        bool bFilterHitSelf = false;
        bool bFilterHitChild = false;
    };

    struct ConfigRegistryContext
    {
       private:
        //! Anchor for lifecycle management
        shared_ptr<void> _anchor = make_shared<nullptr_t>();

       public:
        //! Entity list is constant after initial build
        vector<uint64_t> entityKeys;

        //! Config category descriptor
        notify::config_category_t rootCategoryDesc;

        //! Config contexts
        unordered_map<notify::config_category_t const*, ConfigCategoryContext> categoryContexts;

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
    map<uint64_t, ConfigEntityContext> _allEntities;

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

   public:
    //
    //              Event Handling
    //
    void HandleNewConfigClass(string const& key, notify::config_category_t const& root)
    {
        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind_front(&Self::_handleNewConfigClassMainThread, this, key, root));
    }

    void HandleConfigUpdate(config_entity_update_t const& entity)
    {
        PostEventMainThreadWeak(
                _host->SessionAnchor(),
                bind_front(&Self::_handleConfigsUpdate, this, entity));
    }

   private:
    void _handleNewConfigClassMainThread(string, notify::config_category_t);
    void _handleConfigsUpdate(config_entity_update_t entity) {}

    void _cleanupRegistryContext(ConfigRegistryContext& rg);
    void _recursiveConstructCategories(ConfigRegistryContext* rg, notify::config_category_t const&);
};
}  // namespace widgets
