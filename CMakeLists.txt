cmake_minimum_required(VERSION 3.20)
project(perfkitpp-remote-dashboard-imgui)

find_package(OpenGL REQUIRED COMPONENTS OpenGL)

add_subdirectory(third)
add_subdirectory(third/glfw-3.3.5)
add_subdirectory(third/spdlog-1.9.2)

set(perfkit_BUILD_NET_CLIENT OFF)
add_subdirectory(third/perfkit)

add_executable(
    ${PROJECT_NAME}

    src/main.cpp
    third/imgui/imgui_demo.cpp
)

target_link_libraries(
    ${PROJECT_NAME}

    PRIVATE
    perfkitpp-imgui-bundled 
    perfkit::core
)
