//
// Created by ki608 on 2022-03-14.
//

#pragma once
#include <string>

class ISession
{
   public:
    virtual void FetchSessionDisplayName(std::string*) = 0;

    virtual bool ShouldRenderSessionListEntityContent() const { return false; }

    //! Render content of sessions inside of session list labels
    virtual void RenderSessionListEntityContent() {}

    //! Called when session is being closed
    virtual void OnCloseSession() {}

    //! Ticks this session for rendering. Called only when rendering of this session is allowed.
    virtual void RenderTickSession() {}

    //! Ticks this session. Called always regardless of rendering state.
    virtual void TickSession() {}
};
