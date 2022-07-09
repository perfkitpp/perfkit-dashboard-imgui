#include <GLFW/glfw3.h>  // Will drag system OpenGL headers
#include <imgui.h>
#include <perfkit/configs.h>

#include "Application.hpp"

void Application::tickGraphicsMainThread()
{
    if (std::exchange(_workspacePathChanged, false)) {
        glfwSetWindowTitle(
                glfwGetCurrentContext(),
                fmt::format("Perfkit Dashboard V2 - {}", _workspacePath).c_str());
    }
}
