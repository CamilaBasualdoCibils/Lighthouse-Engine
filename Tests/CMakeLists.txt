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
    MaterialTests.cpp
)
target_compile_definitions(LighthouseEngineTests PRIVATE
    LIGHTHOUSE_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
find_program(SPIRV_VAL_EXECUTABLE spirv-val)
if(SPIRV_VAL_EXECUTABLE)
    target_compile_definitions(LighthouseEngineTests PRIVATE
        LIGHTHOUSE_SPIRV_VAL_EXECUTABLE="${SPIRV_VAL_EXECUTABLE}")
endif()
target_link_libraries(LighthouseEngineTests PRIVATE
    Lighthouse::Engine
    GTest::gtest_main
    GTest::gmock
)

include(GoogleTest)
gtest_discover_tests(LighthouseEngineTests)
