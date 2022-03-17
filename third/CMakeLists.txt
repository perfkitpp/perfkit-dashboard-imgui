project(perfkitpp-imgui-bundled)

add_library(
        ${PROJECT_NAME}

        STATIC
        imgui/imgui.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_demo.cpp
        imgui/backends/imgui_impl_glfw.cpp
        imgui/backends/imgui_impl_opengl3.cpp
        ImGuiColorTextEdit/TextEditor.cpp
        implot/implot.cpp
        implot/implot_items.cpp
        imgui-extension/imgui_extension.cpp
)

target_include_directories(
        ${PROJECT_NAME}

        PUBLIC
        imgui
        imgui/backends
        implot
        ${OpenGL_INCLUDE_DIRS}
        ImGuiColorTextEdit
        imgui-extension
)

target_compile_definitions(
        ${PROJECT_NAME}

        PUBLIC
        IMGUI_DEFINE_MATH_OPERATORS
)

target_link_libraries(
        ${PROJECT_NAME}

        PUBLIC
        glfw
        ${OPENGL_LIBRARIES}
        range-v3::range-v3
)
