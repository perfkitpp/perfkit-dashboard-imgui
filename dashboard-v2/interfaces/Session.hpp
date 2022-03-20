//
// Created by ki608 on 2022-03-14.
//

#pragma once
#include <string>

enum class ESessionType
{
    None = 0,
    TcpUnsafe = 1,
    TcpSsl = 2,
    RelayServer = 3,
    WebSocketUri = 4,

    ENUM_MAX_VALUE,
};

class ISession
{
   public:
    //! Retrieve session's display name. It might do nothing if there's no active connection.
    virtual void FetchSessionDisplayName(std::string*) {}

    //! Initialize session with given string uri.
    virtual void InitializeSession(std::string const& keyUri) = 0;

    //! Returns true if anything should be rendered
    virtual bool ShouldRenderSessionListEntityContent() const { return false; }

    //! Render content of sessions inside of session list labels
    virtual void RenderSessionListEntityContent() {}

    //! Returns whether this session is connected to server
    virtual bool IsSessionOpen() const { return false; }

    //! Close this session. Reconnecting should be handled inside of
    //! the function RenderSessionListEntityContent()
    virtual void CloseSession() {}

    //! Ticks this session for rendering. Called only when rendering of this session is allowed.
    virtual void RenderTickSession() {}

    //! Ticks this session. Called always regardless of rendering state.
    virtual void TickSession() {}
};
