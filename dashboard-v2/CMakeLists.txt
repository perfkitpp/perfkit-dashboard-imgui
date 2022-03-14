project(perfkit-dashboard-v2)

#
# SOURCES
#
set(
        ${PROJECT_NAME}_SOURCES

        main.cpp
        Application.cpp

        interfaces/Session.cpp
        sessions/IndepServerClientSession.cpp
        utils/Notify.cpp
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
        perfkit::asio
)

target_include_directories(
        ${PROJECT_NAME}

        PRIVATE
        .
)

target_precompile_headers(
        ${PROJECT_NAME}

        PRIVATE
        stdafx.h
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
set_target_properties(
        ${PROJECT_NAME}

        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${PROJECT_NAME}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

configure_file(
        ${CMAKE_SOURCE_DIR}/misc/CascadiaMono.ttf
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/fonts/font-0.ttf
        COPYONLY
)

configure_file(
        ${CMAKE_SOURCE_DIR}/misc/NanumGothic.ttf
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/fonts/font-1.ttf
        COPYONLY
)
