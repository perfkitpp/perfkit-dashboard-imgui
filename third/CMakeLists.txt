include(FetchContent)

### CONFIGURE BUNDLED IMGUI
project(perfkitpp-imgui-bundled)
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/heads/docking.zip)
FetchContent_Populate(imgui)

add_library(
        ${PROJECT_NAME}

        STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ImGuiColorTextEdit/TextEditor.cpp
        implot/implot.cpp
        implot/implot_items.cpp
        imgui-extension/imgui_extension.cpp
)

target_include_directories(
        ${PROJECT_NAME}

        PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        implot
        ${OpenGL_INCLUDE_DIRS}
        ImGuiColorTextEdit
        imgui-extension
)

target_compile_features(
        ${PROJECT_NAME}

        PUBLIC
        cxx_std_17
)

target_link_libraries(
        ${PROJECT_NAME}

        PUBLIC
        glfw
        ${OPENGL_LIBRARIES}
        range-v3::range-v3
)
