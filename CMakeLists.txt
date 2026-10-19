cmake_minimum_required(VERSION 3.20)

project(TFM_PathTracer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# SDL3 setup
set(SDL_SHARED OFF CACHE BOOL "Build SDL shared library" FORCE)
set(SDL_STATIC ON CACHE BOOL "Build SDL static library" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "Build SDL test library" FORCE)

add_subdirectory(third-party/SDL EXCLUDE_FROM_ALL)

# OpenGL setup
find_package(OpenGL REQUIRED)

# GLAD setup (OpenGL 4.6 Core)
add_library(glad STATIC
    third-party/glad/src/glad.c
)
target_include_directories(glad PUBLIC
    third-party/glad/include
)

# ImGui setup
set(IMGUI_DIR third-party/imgui)
add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    third-party/SDL/include
    third-party/glad/include
)
target_link_libraries(imgui PRIVATE glad)

# Main executable
set(SOURCES
    src/main.cpp
    src/renderer/Camera.cpp
    src/renderer/ComputeShader.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/FullscreenQuad.cpp
    src/renderer/Model.cpp
    src/renderer/PathTracerPipeline.cpp
    src/renderer/Shader.cpp
    src/renderer/Texture.cpp
    src/renderer/Texture3D.cpp
)

add_executable(tfm_pathtracer ${SOURCES})

target_include_directories(tfm_pathtracer PRIVATE
    include
    third-party
    third-party/glad/include
    third-party/glm
    third-party/json
    third-party/stb
    third-party/imgui
    third-party/SDL/include
)

target_link_libraries(tfm_pathtracer PRIVATE
    SDL3::SDL3
    glad
    imgui
    OpenGL::GL
)

if(UNIX AND NOT APPLE)
    target_link_libraries(tfm_pathtracer PRIVATE
        dl
        pthread
    )
endif()

target_compile_definitions(tfm_pathtracer PRIVATE
    SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)
