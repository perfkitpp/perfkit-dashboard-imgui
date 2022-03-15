//
// Created by ki608 on 2022-03-14.
//

#pragma once
#include <string>

enum class ESessionType
{
    None            = 0,
    TcpRawClient    = 1,
    WebSocketClient = 2,

    ENUM_MAX_VALUE,
};

class ISession
{
   public:
    //! Retrieve session's display name.
    //! It may do nothing if there's no active connection.
    virtual void FetchSessionDisplayName(std::string*) {}

    //! Initialize session with given string uri.
    virtual void InitializeSession(std::string const& keyUri) = 0;

    //! Returns true if anything should be rendered
    virtual bool ShouldRenderSessionListEntityContent() const { return false; }

    //! Render content of sessions inside of session list labels
    virtual void RenderSessionListEntityContent() {}

    //! Called when session is being closed.
    //! After this function called, destructor will be invoked.
    virtual void OnCloseSession() {}

    //! Ticks this session for rendering. Called only when rendering of this session is allowed.
    virtual void RenderTickSession() {}

    //! Ticks this session. Called always regardless of rendering state.
    virtual void TickSession() {}
};
