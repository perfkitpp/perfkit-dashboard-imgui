//
// Created by ki608 on 2022-03-18.
//

#pragma once
#include <unordered_map>

#include <nlohmann/json.hpp>

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

   private:
    //
    // CONFIGS
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
    };

    struct ConfigCategoryContext
    {
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
        //! Weak reference to edit target
        weak_ptr<ConfigEntityContext> entityRef;
    };

   private:
    IRpcSessionOwner* _host;

    //! List of config registry contexts.
    map<string, ConfigRegistryContext> _ctxs;

    //!
    EditContext _edit;

    //! All config entities
    map<uint64_t, ConfigEntityContext> _allEntities;

   public:
    explicit ConfigWindow(IRpcSessionOwner* host) noexcept : _host(host) {}

   public:
    void Tick() {}
    void RenderMainWnd();
    void ClearContexts() {}

   public:
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
