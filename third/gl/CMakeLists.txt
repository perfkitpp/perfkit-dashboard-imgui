project(perfkit-bundled-gl3w)
add_library(${PROJECT_NAME} STATIC)

target_sources(
        ${PROJECT_NAME}

        PRIVATE
        src/gl3w.c
)

target_include_directories(
        ${PROJECT_NAME}

        PUBLIC
        include
)
