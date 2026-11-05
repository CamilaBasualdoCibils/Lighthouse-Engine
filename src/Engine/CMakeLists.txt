find_package(EnTT CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
find_package(glm CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)
find_package(spdlog CONFIG REQUIRED)

file(GLOB_RECURSE _engine_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(LighthouseEngine STATIC ${_engine_sources})

if(LIGHTHOUSE_IMGUI)
    find_package(imgui CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
    target_sources(LighthouseEngine PRIVATE ImGuiTechnique.cpp)
    target_link_libraries(LighthouseEngine PUBLIC imgui::imgui implot::implot)
    target_compile_definitions(LighthouseEngine PUBLIC LIGHTHOUSE_IMGUI=1)
endif()
if (LIGHTHOUSE_ENABLE_TRACY)
    target_compile_definitions(LighthouseEngine PUBLIC LIGHTHOUSE_ENABLE_TRACY=1)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(LighthouseEngine PUBLIC Tracy::TracyClient)
endif()
add_library(Lighthouse::Engine ALIAS LighthouseEngine)

target_include_directories(LighthouseEngine PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(LighthouseEngine PRIVATE ${TINYGLTF_INCLUDE_DIR})
target_include_directories(LighthouseEngine PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(LighthouseEngine PUBLIC EnTT::EnTT glm::glm fg::FrameGraph PRIVATE glfw PkgConfig::WAYLAND_EGL
    glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits slang::slang $<IF:$<TARGET_EXISTS:mimalloc-static>,mimalloc-static,mimalloc> spdlog::spdlog)
target_compile_features(LighthouseEngine PUBLIC cxx_std_23)

