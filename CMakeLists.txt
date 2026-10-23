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

option(TFM_BUILD_BENCHMARKS "Build the lighting quality benchmark" OFF)
if(TFM_BUILD_BENCHMARKS)
    set(BENCHMARK_RENDER_SOURCES ${SOURCES})
    list(REMOVE_ITEM BENCHMARK_RENDER_SOURCES src/main.cpp)
    add_executable(tfm_lighting_benchmark tests/lighting_benchmark.cpp ${BENCHMARK_RENDER_SOURCES})
    target_include_directories(tfm_lighting_benchmark PRIVATE
        $<TARGET_PROPERTY:tfm_pathtracer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(tfm_lighting_benchmark PRIVATE
        $<TARGET_PROPERTY:tfm_pathtracer,COMPILE_DEFINITIONS>)
    target_link_libraries(tfm_lighting_benchmark PRIVATE SDL3::SDL3 glad OpenGL::GL)
    enable_testing()
    add_executable(tfm_lighting_metrics_tests tests/lighting_metrics_tests.cpp)
    target_include_directories(tfm_lighting_metrics_tests PRIVATE include third-party)
    add_test(NAME lighting_metrics COMMAND tfm_lighting_metrics_tests)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME lighting_benchmark_smoke COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmark_smoke.py $<TARGET_FILE:tfm_lighting_benchmark>)
        set_tests_properties(lighting_benchmark_smoke PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endif()
endif()

target_compile_definitions(tfm_pathtracer PRIVATE
    SHADERS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

option(TFM_BUILD_RENDER_TESTS "Build GPU regression tests" OFF)
if(TFM_BUILD_RENDER_TESTS)
    enable_testing()
    set(RENDER_TEST_SOURCES ${SOURCES})
    list(REMOVE_ITEM RENDER_TEST_SOURCES src/main.cpp)
    add_executable(tfm_render_tests tests/render_tests.cpp ${RENDER_TEST_SOURCES})
    target_include_directories(tfm_render_tests PRIVATE
        $<TARGET_PROPERTY:tfm_pathtracer,INCLUDE_DIRECTORIES>)
    target_compile_definitions(tfm_render_tests PRIVATE
        $<TARGET_PROPERTY:tfm_pathtracer,COMPILE_DEFINITIONS>)
    target_link_libraries(tfm_render_tests PRIVATE SDL3::SDL3 glad OpenGL::GL)
    add_test(NAME render_regressions COMMAND tfm_render_tests)
endif()
