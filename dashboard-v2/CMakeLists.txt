project(perfkit-dashboard-v2)

#
# SOURCES
#
set(
        ${PROJECT_NAME}_SOURCES

        main.cpp
)

#
# EXECUTABLE
#
if (MSVC)
    add_executable(${PROJECT_NAME} WIN32 ${${PROJECT_NAME}_SOURCES})
else ()
    add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES})
endif ()

#
# DEPENDENCIES
#
target_link_libraries(
        ${PROJECT_NAME}

        PRIVATE
        perfkitpp-imgui-bundled
        perfkit::core
        perfkit::net-v2::protocol
)

#
# WIN32
#
if (MSVC)
    target_compile_options(
            ${PROJECT_NAME}

            PRIVATE
            /utf-8
    )
endif ()


#
# INSTALL SCRIPT
#
