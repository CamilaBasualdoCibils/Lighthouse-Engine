find_package(EnTT CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
add_library(LighthouseEngine
    Entity.cpp
    Scene.cpp
    GraphicsBackend.cpp
    WindowManager.cpp
    RenderView.cpp
    Renderer.cpp
    EngineRuntime.cpp
)
add_library(Lighthouse::Engine ALIAS LighthouseEngine)

target_include_directories(LighthouseEngine PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(LighthouseEngine PUBLIC EnTT::EnTT glm::glm PRIVATE glfw)
target_compile_features(LighthouseEngine PUBLIC cxx_std_23)

