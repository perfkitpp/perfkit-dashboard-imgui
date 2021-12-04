project(perfkit-dashboard)

add_executable(
        ${PROJECT_NAME}

        main.cpp
        imgui-extension.cpp
        ../third/imgui/imgui_demo.cpp

        app/session_slot.cpp
        app/application.cpp
        classes/session_context.cpp
        classes/connection/plain_tcp.cpp
)

target_link_libraries(
        ${PROJECT_NAME}

        PRIVATE
        perfkitpp-imgui-bundled
        perfkit::core
        perfkit::picosha2
        perfkit::net::asio
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
)
