cmake_minimum_required(VERSION 3.20)
project(perfkitpp-remote-dashboard-imgui)

find_package(OpenGL REQUIRED COMPONENTS OpenGL)
find_package(spdlog QUIET)

add_subdirectory(third)

set(perfkit_BUILD_NET_CLIENT OFF)
add_subdirectory(third/perfkit)

#add_subdirectory(dashboard-v1)
add_subdirectory(dashboard-v2)

