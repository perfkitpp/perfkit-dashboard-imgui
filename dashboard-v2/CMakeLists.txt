project(perfkit-dashboard-v2)

#
# SOURCES
#
set(
        ${PROJECT_NAME}_SOURCES

        main.cpp
        Application.cpp
        Application-Graphics.cpp

        utils/Notify.cpp
        utils/Misc.cpp
        utils/TimePlot.cpp
        utils/JsonEdit.cpp

        sessions/BasicPerfkitNetClient.cpp
        sessions/BasicPerfkitNetClient-SessionBuilder.cpp
        sessions/PerfkitTcpRawClient.cpp

        widgets/ConfigWindow.cpp
        widgets/TraceWindow.cpp
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

target_compile_features(
        ${PROJECT_NAME}

        PRIVATE
        cxx_std_17
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
            /Zc:__cplusplus
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
        ${CMAKE_SOURCE_DIR}/misc/JetBrainsMono-Regular.ttf
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/rsrc/fonts/font-0.ttf
        COPYONLY
)

configure_file(
        ${CMAKE_SOURCE_DIR}/misc/NanumGothic.ttf
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/rsrc/fonts/font-1.ttf
        COPYONLY
)
