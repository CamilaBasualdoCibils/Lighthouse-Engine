find_package(EnTT CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
find_package(glm CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIR NAMES tiny_gltf.h REQUIRED)
set(LIGHTHOUSE_SHADER_DIR "${PROJECT_SOURCE_DIR}/src/renderer/shaders/materials")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${LIGHTHOUSE_SHADER_DIR}/LighthouseMaterial.slang"
    "${LIGHTHOUSE_SHADER_DIR}/LighthouseLit.slang"
    "${LIGHTHOUSE_SHADER_DIR}/LighthouseUnlit.slang"
    "${LIGHTHOUSE_SHADER_DIR}/MaterialEntries.slang.in"
    "${LIGHTHOUSE_SHADER_DIR}/MaterialEntriesUnlit.slang.in")
file(READ "${LIGHTHOUSE_SHADER_DIR}/LighthouseMaterial.slang" LIGHTHOUSE_MATERIAL_SOURCE)
file(READ "${LIGHTHOUSE_SHADER_DIR}/LighthouseLit.slang" LIGHTHOUSE_LIT_SOURCE)
file(READ "${LIGHTHOUSE_SHADER_DIR}/LighthouseUnlit.slang" LIGHTHOUSE_UNLIT_SOURCE)
file(READ "${LIGHTHOUSE_SHADER_DIR}/MaterialEntries.slang.in" LIGHTHOUSE_LIT_ENTRY_SOURCE)
file(READ "${LIGHTHOUSE_SHADER_DIR}/MaterialEntriesUnlit.slang.in" LIGHTHOUSE_UNLIT_ENTRY_SOURCE)
configure_file(BuiltinShaderSources.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/BuiltinShaderSources.hpp" @ONLY)
add_library(LighthouseEngine
    Application.cpp
    Entity.cpp
    Scene.cpp
    WorldRenderer.cpp
    GraphicsBackend.cpp
    WindowManager.cpp
    RenderView.cpp
    SwapchainTechniques.cpp
    RenderGraph.cpp
    Renderer.cpp
    RenderMesh.cpp
    AssetMesh.cpp
    GLTFLoader.cpp
    Shader.cpp
    ShaderCompiler.cpp
    MaterialCompiler.cpp
    RenderMeshManager.cpp
    EngineRuntime.cpp
)
if(LIGHTHOUSE_IMGUI)
    find_package(imgui CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
    target_sources(LighthouseEngine PRIVATE ImGuiTechnique.cpp)
    target_link_libraries(LighthouseEngine PUBLIC imgui::imgui implot::implot)
    target_compile_definitions(LighthouseEngine PUBLIC LIGHTHOUSE_IMGUI=1)
endif()
add_library(Lighthouse::Engine ALIAS LighthouseEngine)

target_include_directories(LighthouseEngine PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(LighthouseEngine PRIVATE ${TINYGLTF_INCLUDE_DIR})
target_include_directories(LighthouseEngine PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(LighthouseEngine PUBLIC EnTT::EnTT glm::glm fg::FrameGraph PRIVATE glfw PkgConfig::WAYLAND_EGL
    glslang::glslang glslang::SPIRV glslang::glslang-default-resource-limits slang::slang)
target_compile_features(LighthouseEngine PUBLIC cxx_std_23)

