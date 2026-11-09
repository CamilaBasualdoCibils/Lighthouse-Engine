cmake_minimum_required(VERSION 3.14)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT}
   AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE FILEPATH "CMake toolchain file")
endif()
project(Lighthouse VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LIGHTHOUSE_ENABLE_TRACY "Enable Tracy profiling instead of mimalloc" OFF)
option(LIGHTHOUSE_IMGUI "Build ImGui views and select GLFW X11/XWayland (OFF selects Wayland)" ON)
option(LIGHTHOUSE_BUILD_EXAMPLES "Build Lighthouse examples" OFF)
option(LIGHTHOUSE_BUILD_OPENGL_BACKEND "Build the optional OpenGL backend" ON)
option(LIGHTHOUSE_BUILD_VULKAN_BACKEND "Build the optional Vulkan backend" ON)
option(BUILD_TESTING "Build Lighthouse tests" OFF)
include(CTest)

# Build dependency libraries by default; their tests, tools, and examples are opt-in.
option(OPENPERIPHERAL_BUILD_TESTS "Build OpenPeripheral tests" OFF)
option(OPENPERIPHERAL_BUILD_TOOLS "Build OpenPeripheral tools" OFF)
option(OPENPERIPHERAL_BUILD_EXAMPLES "Build OpenPeripheral examples" OFF)
option(VISR_BUILD_TESTS "Build VISR tests" OFF)
option(VISR_BUILD_TOOLS "Build VISR tools" OFF)

include(cmake/EmbedResources.cmake)
lighthouse_embed_resources(LighthouseEngineEmbedded "Lighthouse::EngineEmbedded"
    "Lighthouse/Engine/EmbeddedResources.hpp" "${CMAKE_CURRENT_SOURCE_DIR}/res")
add_library(Lighthouse::EngineEmbedded ALIAS LighthouseEngineEmbedded)
target_include_directories(LighthouseEngineEmbedded PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")

add_subdirectory(extern/OpenPeripherals)
add_subdirectory(extern/VISR)
add_subdirectory(src/Engine)

if(BUILD_TESTING)
    add_subdirectory(Tests)
endif()

if(LIGHTHOUSE_BUILD_OPENGL_BACKEND OR LIGHTHOUSE_BUILD_EXAMPLES)
    add_subdirectory(src/Backends/Render/OpenGL)
endif()
if(LIGHTHOUSE_BUILD_VULKAN_BACKEND OR LIGHTHOUSE_BUILD_EXAMPLES)
    add_subdirectory(src/Backends/Render/Vulkan)
endif()

if(LIGHTHOUSE_BUILD_EXAMPLES)
    add_subdirectory(Examples)
endif()


include(cmake/GetDeps.cmake)

option(LIGHTHOUSE_BUILD_GLFW_INPUT_BACKEND "Build the GLFW input backend" ON)
if(LIGHTHOUSE_BUILD_GLFW_INPUT_BACKEND OR LIGHTHOUSE_BUILD_EXAMPLES)
    add_subdirectory(src/Backends/Input/GLFW)
endif()
