include(FetchContent)

### CONFIGURE BUNDLED IMGUI
project(perfkitpp-imgui-bundled)
add_library(${PROJECT_NAME} INTERFACE)

FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/heads/docking.zip)
FetchContent_Populate(imgui)

FetchContent_Declare(implot URL https://github.com/epezent/implot/archive/refs/heads/master.zip)
FetchContent_Populate(implot)

FetchContent_Declare(glfw URL https://github.com/glfw/glfw/archive/refs/tags/3.3.7.tar.gz)
FetchContent_Populate(glfw)
add_subdirectory(${glfw_SOURCE_DIR} ${glfw_BINARY_DIR} EXCLUDE_FROM_ALL)

target_sources(
        ${PROJECT_NAME}

        INTERFACE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
        ImGuiColorTextEdit/TextEditor.cpp
        imgui-extension/imgui_extension.cpp
)

target_include_directories(
        ${PROJECT_NAME}

        INTERFACE
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
        ${OpenGL_INCLUDE_DIRS}
        ImGuiColorTextEdit
        imgui-extension
)

target_compile_features(
        ${PROJECT_NAME}

        INTERFACE
        cxx_std_17
)

target_link_libraries(
        ${PROJECT_NAME}

        INTERFACE
        glfw
        ${OPENGL_LIBRARIES}
        range-v3::range-v3
)

### CONFIGURE BUNDLED GL3W
add_subdirectory(gl)

