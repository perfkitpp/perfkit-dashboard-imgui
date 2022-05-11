project(perfkit-dashboard-v2)


#
# EXECUTABLE
#
if (MSVC)
    add_executable(${PROJECT_NAME} WIN32 ${${PROJECT_NAME}_SOURCES})
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rsrc/icon.rc)
else ()
    add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES})
endif ()

#
# SOURCES
#
target_sources(
        ${PROJECT_NAME} PRIVATE

        main.cpp
        Application.cpp
        Application-Graphics.cpp

        utils/Notify.cpp
        utils/Misc.cpp
        utils/TimePlotSlotProxy.cpp
        utils/JsonEdit.cpp

        sessions/BasicPerfkitNetClient.cpp
        sessions/BasicPerfkitNetClient-SessionBuilder.cpp
        sessions/PerfkitTcpRawClient.cpp
        sessions/SessionDiscoverAgent.cpp

        widgets/ConfigWindow.cpp
        widgets/TraceWindow.cpp
        widgets/TimePlot.cpp
)

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
# Resource configuration for debugging
#
configure_file(
        ${CMAKE_SOURCE_DIR}/misc/JetBrainsMono-Regular.ttf
        ${CMAKE_CURRENT_BINARY_DIR}/rsrc/fonts/font-0.ttf
        COPYONLY
)

configure_file(
        ${CMAKE_SOURCE_DIR}/misc/NanumGothic.ttf
        ${CMAKE_CURRENT_BINARY_DIR}/rsrc/fonts/font-1.ttf
        COPYONLY
)

#
# Install configuration
#
install(
        TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION ${PROJECT_NAME}
)

install(
        FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
        OPTIONAL
        DESTINATION ${PROJECT_NAME}
)

install(
        DIRECTORY
        ${CMAKE_CURRENT_BINARY_DIR}/rsrc

        DESTINATION
        ${PROJECT_NAME}
)



