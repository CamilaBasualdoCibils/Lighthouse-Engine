find_package(GTest CONFIG REQUIRED)

add_executable(LighthouseEngineTests
    SceneEntityTests.cpp
    GraphicsBackendTests.cpp
    WindowManagerTests.cpp
    RendererTests.cpp
    WorldRendererTests.cpp
    MeshTests.cpp
    GLTFTests.cpp
    ShaderTests.cpp
)
target_link_libraries(LighthouseEngineTests PRIVATE
    Lighthouse::Engine
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(LighthouseEngineTests)
