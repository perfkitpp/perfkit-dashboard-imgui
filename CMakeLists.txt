cmake_minimum_required(VERSION 3.20)
project(perfkitpp-remote-dashboard-imgui)

find_package(OpenGL REQUIRED COMPONENTS OpenGL)

add_subdirectory(third)
add_subdirectory(third/glfw-3.3.5 EXCLUDE_FROM_ALL)
add_subdirectory(third/spdlog-1.9.2 EXCLUDE_FROM_ALL)

set(perfkit_BUILD_NET_CLIENT OFF)
add_subdirectory(third/perfkit)

add_subdirectory(src)

configure_file(
        misc/CascadiaMono.ttf
        ${CMAKE_BINARY_DIR}/font-0.ttf
        COPYONLY
)

configure_file(
        misc/NanumGothic.ttf
        ${CMAKE_BINARY_DIR}/font-1.ttf
        COPYONLY
)
