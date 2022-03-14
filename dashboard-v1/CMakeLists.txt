project(perfkit-dashboard)

set(
        PERFKIT_DASHBOARD_SOURCES

        main.cpp
        imgui-extension.cpp
        platform.cpp

        app/session_slot.cpp
        app/session_slot_trace_context.cpp
        app/application.cpp
        app/utility.cpp

        classes/session_context.cpp
        classes/connection/plain_tcp.cpp
)

if (MSVC)
    add_executable(
            ${PROJECT_NAME}

            WIN32
            ${PERFKIT_DASHBOARD_SOURCES}
    )
else ()
    add_executable(
            ${PROJECT_NAME}

            ${PERFKIT_DASHBOARD_SOURCES}
    )
endif ()

target_link_libraries(
        ${PROJECT_NAME}

        PRIVATE
        perfkitpp-imgui-bundled
        perfkit::core
        perfkit::picosha2
        perfkit::net::core
        perfkit::asio
)

target_include_directories(
        ${PROJECT_NAME}

        PRIVATE
        ../third/perfkit/extensions/net/include/perfkit/extension/net-internals
        .
)

target_compile_definitions(
        ${PROJECT_NAME}

        PRIVATE
        -D_WIN32_WINNT=0x0601
        -D_CRT_SECURE_NO_WARNINGS=1
)

if (MSVC)
    add_compile_options(/utf-8)

endif ()

set_target_properties(
        ${PROJECT_NAME}

        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/perfkit-dashboard"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

configure_file(
        ${CMAKE_SOURCE_DIR}/misc/CascadiaMono.ttf
        ${CMAKE_BINARY_DIR}/perfkit-dashboard/fonts/font-0.ttf
        COPYONLY
)

configure_file(
        ${CMAKE_SOURCE_DIR}/misc/NanumGothic.ttf
        ${CMAKE_BINARY_DIR}/perfkit-dashboard/fonts/font-1.ttf
        COPYONLY
)
