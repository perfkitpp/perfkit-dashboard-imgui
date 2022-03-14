cmake_minimum_required(VERSION 3.20)
project(perfkitpp-remote-dashboard-imgui)

find_package(OpenGL REQUIRED COMPONENTS OpenGL)

add_subdirectory(third)
add_subdirectory(third/glfw-3.3.5 EXCLUDE_FROM_ALL)
add_subdirectory(third/spdlog-1.9.2 EXCLUDE_FROM_ALL)

set(perfkit_BUILD_NET_CLIENT OFF)
add_subdirectory(third/perfkit)

add_subdirectory(dashboard-v1)
add_subdirectory(dashboard-v2)

