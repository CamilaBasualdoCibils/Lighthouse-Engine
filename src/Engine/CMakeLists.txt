find_package(EnTT CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
find_package(glm CONFIG REQUIRED)
add_library(LighthouseEngine
    Application.cpp
    Entity.cpp
    Scene.cpp
    GraphicsBackend.cpp
    WindowManager.cpp
    RenderView.cpp
    RenderGraph.cpp
    Renderer.cpp
    Mesh.cpp
    MeshManager.cpp
    EngineRuntime.cpp
)
if(LIGHTHOUSE_IMGUI)
    find_package(imgui CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
    target_sources(LighthouseEngine PRIVATE ImGuiTechniques.cpp)
    target_link_libraries(LighthouseEngine PUBLIC imgui::imgui implot::implot)
    target_compile_definitions(LighthouseEngine PUBLIC LIGHTHOUSE_IMGUI=1)
endif()
add_library(Lighthouse::Engine ALIAS LighthouseEngine)

target_include_directories(LighthouseEngine PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(LighthouseEngine PUBLIC EnTT::EnTT glm::glm fg::FrameGraph PRIVATE glfw PkgConfig::WAYLAND_EGL)
target_compile_features(LighthouseEngine PUBLIC cxx_std_23)

