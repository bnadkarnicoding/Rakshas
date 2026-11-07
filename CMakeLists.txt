# Rakshas Engine — top-level build.
# See docs/ARCHITECTURE.md for how the pieces fit together.
cmake_minimum_required(VERSION 3.24)
project(rakshas VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(RK_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (Linux/Clang/GCC)" OFF)
option(RK_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# Dependencies (approved list only — see docs/ARCHITECTURE.md section 1)
# ---------------------------------------------------------------------------
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        release-3.2.24
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL3)

# ---------------------------------------------------------------------------
# Shared compile settings for OUR code (not third-party code)
# ---------------------------------------------------------------------------
add_library(rk_settings INTERFACE)
if(MSVC)
  target_compile_options(rk_settings INTERFACE /W4 /permissive- /utf-8)
  if(RK_WARNINGS_AS_ERRORS)
    target_compile_options(rk_settings INTERFACE /WX)
  endif()
  target_compile_definitions(rk_settings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
  target_compile_options(rk_settings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Wno-missing-field-initializers)
  if(RK_WARNINGS_AS_ERRORS)
    target_compile_options(rk_settings INTERFACE -Werror)
  endif()
  if(RK_SANITIZE)
    target_compile_options(rk_settings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(rk_settings INTERFACE -fsanitize=address,undefined)
  endif()
endif()

# ---------------------------------------------------------------------------
# Engine libraries (layers: core -> platform/render -> game)
# ---------------------------------------------------------------------------
add_library(rk_core STATIC
  engine/core/log.cpp
  engine/core/file.cpp
  engine/core/data.cpp)
target_include_directories(rk_core PUBLIC engine)
target_link_libraries(rk_core PUBLIC rk_settings)

add_library(rk_platform STATIC
  engine/platform/platform_sdl.cpp)
target_link_libraries(rk_platform PUBLIC rk_core PRIVATE SDL3::SDL3-static)

add_library(rk_render STATIC
  engine/render/vulkan/vk_device.cpp)
target_link_libraries(rk_render PUBLIC rk_core PRIVATE Vulkan::Vulkan)

# ---------------------------------------------------------------------------
# Shaders: GLSL -> SPIR-V, placed next to the executable in shaders/
# ---------------------------------------------------------------------------
set(RK_SHADERS
  shaders/sky.vert shaders/sky.frag
  shaders/lit.vert shaders/lit.frag
  shaders/shadow.vert)
set(RK_SHADER_OUT "${CMAKE_BINARY_DIR}/bin/shaders")
set(RK_SPV_FILES "")
foreach(src ${RK_SHADERS})
  get_filename_component(name ${src} NAME)
  set(out "${RK_SHADER_OUT}/${name}.spv")
  add_custom_command(
    OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RK_SHADER_OUT}
    COMMAND Vulkan::glslc --target-env=vulkan1.3 -I ${CMAKE_SOURCE_DIR}/shaders
            -o ${out} ${CMAKE_SOURCE_DIR}/${src}
    DEPENDS ${CMAKE_SOURCE_DIR}/${src} ${CMAKE_SOURCE_DIR}/shaders/common.glsl
    COMMENT "Compiling shader ${name}")
  list(APPEND RK_SPV_FILES ${out})
endforeach()
add_custom_target(rk_shaders ALL DEPENDS ${RK_SPV_FILES})

# ---------------------------------------------------------------------------
# The game application
# ---------------------------------------------------------------------------
add_executable(rakshas
  game/main.cpp
  game/shapes.cpp
  game/region.cpp
  game/sky_model.cpp
  game/screenshot.cpp)
target_include_directories(rakshas PRIVATE game third_party)
target_link_libraries(rakshas PRIVATE rk_core rk_platform rk_render)
add_dependencies(rakshas rk_shaders)
# Content files are copied on every build, so edits always show up.
add_custom_target(rk_content ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/content ${CMAKE_BINARY_DIR}/bin/content
  COMMENT "Copying content files")
add_dependencies(rakshas rk_content)
set_target_properties(rakshas PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(RK_BUILD_TESTS)
  enable_testing()
  add_executable(rk_tests tests/test_main.cpp tests/test_math.cpp tests/test_sky_model.cpp tests/test_data.cpp tests/test_region.cpp
    game/sky_model.cpp game/shapes.cpp game/region.cpp)
  target_compile_definitions(rk_tests PRIVATE RK_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_include_directories(rk_tests PRIVATE game tests)
  target_link_libraries(rk_tests PRIVATE rk_core)
  add_test(NAME unit_tests COMMAND rk_tests)
endif()
