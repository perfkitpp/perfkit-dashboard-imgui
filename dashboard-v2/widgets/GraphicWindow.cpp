//
// Created by ki608 on 2022-07-09.
//

#include "GraphicWindow.hpp"

#include "graphics/GraphicContext.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3_loader.h"

void widgets::GraphicWindow::Tick()
{
    // if(_context) { _context->flush(); }
}

void widgets::GraphicWindow::Render()
{
    if (not _context) {
    }
}

widgets::GraphicWindow::GraphicWindow(IRpcSessionOwner* host) : _host(host) {}
widgets::GraphicWindow::~GraphicWindow() = default;
