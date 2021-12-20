project(perfkitpp-imgui-bundled)

add_library(
        ${PROJECT_NAME}

        STATIC
        imgui/imgui.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/imgui_draw.cpp
        imgui/backends/imgui_impl_glfw.cpp
        imgui/backends/imgui_impl_opengl3.cpp
        ImGuiColorTextEdit/TextEditor.cpp
)

target_include_directories(
        ${PROJECT_NAME}

        PUBLIC
        imgui
        imgui/backends
        ${OpenGL_INCLUDE_DIRS}
        ImGuiColorTextEdit
)

target_link_libraries(
        ${PROJECT_NAME}

        PUBLIC
        glfw
        ${OPENGL_LIBRARIES}
        range-v3::range-v3
)
