cmake_minimum_required(VERSION 3.20)
project(Photo2Scene VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # helps VS Code IntelliSense

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(P2S_BUILD_TESTS "Build unit tests" ON)

# ---------------------------------------------------------------------------
# Dependencies (downloaded automatically on first configure)
# ---------------------------------------------------------------------------
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)  # Linux only: X11 is enough

FetchContent_Declare(glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE)
FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw glm imgui)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# GLAD (generated for OpenGL 4.1 core, checked in under external/glad)
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

# stb_image / stb_image_write (single headers, checked in under external/stb)
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE external/stb)

# nlohmann/json (single header under external/nlohmann)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE external)

# Dear ImGui (no CMake of its own)
add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw OpenGL::GL ${CMAKE_DL_LIBS})

# ---------------------------------------------------------------------------
# Core library: code that does not need a window (unit-testable)
# ---------------------------------------------------------------------------
add_library(p2s_core STATIC
  src/ImageLoader.cpp
  src/MeshData.cpp
  src/Reconstruction.cpp
  src/Sky.cpp
  src/Terrain.cpp)
target_include_directories(p2s_core PUBLIC include)
target_link_libraries(p2s_core PUBLIC glm::glm stb nlohmann_json)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(Photo2Scene
  src/main.cpp
  src/App.cpp
  src/UI.cpp
  src/Renderer.cpp
  src/Camera.cpp
  src/Lighting.cpp
  src/Mesh.cpp
  src/Shader.cpp
  src/Scene.cpp
  src/Texture.cpp
  src/DepthEstimator.cpp
  src/World.cpp)
target_link_libraries(Photo2Scene PRIVATE p2s_core glad glfw imgui_lib OpenGL::GL Threads::Threads nlohmann_json)
target_compile_definitions(Photo2Scene PRIVATE P2S_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

if(MSVC)
  target_compile_options(Photo2Scene PRIVATE /W4 /utf-8)
  target_compile_options(p2s_core PRIVATE /utf-8)
else()
  target_compile_options(Photo2Scene PRIVATE -Wall -Wextra)
endif()

if(APPLE)
  # OpenGL is deprecated (not removed) on macOS; silence the noise.
  target_compile_definitions(Photo2Scene PRIVATE GL_SILENCE_DEPRECATION)
  target_compile_definitions(imgui_lib PRIVATE GL_SILENCE_DEPRECATION)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(P2S_BUILD_TESTS)
  enable_testing()
  add_executable(p2s_tests tests/test_main.cpp)
  target_link_libraries(p2s_tests PRIVATE p2s_core)
  add_test(NAME reconstruction COMMAND p2s_tests)
endif()
