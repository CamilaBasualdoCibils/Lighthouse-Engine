#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>
#include "OpenGLShaderTranslator.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace {
using namespace Lighthouse;

TEST(OpenGLShaderTests, TranslatesCanonicalSpirvToGLSLWithoutAnOpenGLContext) {
    constexpr std::string_view source = R"(#version 450
layout(set = 2, binding = 5) uniform sampler2D inputTexture;
layout(location = 0) out vec4 color;
void main() { color = texture(inputTexture, vec2(0.5)); }
)";
    const auto shader = ShaderCompiler::Compile(source,
        {ShaderSourceLanguage::GLSL, ShaderStage::Fragment, "main"});
    const auto glsl = Detail::TranslateShaderToOpenGL(shader.GetSPIRV(), shader.GetStage(),
        shader.GetEntryPoint(), {16, 16, 32, 8});

    EXPECT_NE(glsl.find("#version 450"), std::string::npos);
    EXPECT_NE(glsl.find("void main()"), std::string::npos);
    EXPECT_EQ(glsl.find("set ="), std::string::npos);
    EXPECT_NE(glsl.find("binding = 0"), std::string::npos);
}

TEST(OpenGLShaderTests, RemapsEqualBindingsFromDifferentDescriptorSets) {
    constexpr std::string_view source = R"(#version 450
layout(set = 0, binding = 0) uniform FrameData { mat4 view; } frameData;
layout(set = 1, binding = 0) uniform ObjectData { mat4 model; } objectData;
void main() { gl_Position = frameData.view * objectData.model * vec4(0.0, 0.0, 0.0, 1.0); }
)";
    const auto shader = ShaderCompiler::Compile(source,
        {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
    DescriptorBindingLocations locations{
        {{0, 0}, {DescriptorType::UniformBuffer, 0, 1}},
        {{1, 0}, {DescriptorType::UniformBuffer, 1, 1}}
    };
    const auto glsl = Detail::TranslateShaderToOpenGL(shader.GetSPIRV(), shader.GetStage(),
        shader.GetEntryPoint(), {16, 16, 32, 8}, &locations);
    EXPECT_NE(glsl.find("binding = 0"), std::string::npos);
    EXPECT_NE(glsl.find("binding = 1"), std::string::npos);
}

TEST(OpenGLShaderTests, CreatesAndDestroysModulesAndLinksGraphicsPipeline) {
    WindowManager windows;
    WindowHandle windowHandle;
    try {
        windowHandle = windows.CreateWindow({
            .title = "OpenGL shader test", .width = 64, .height = 64,
            .eglSurface = true, .visible = false
        });
    } catch (const std::runtime_error& error) {
        GTEST_SKIP() << "Native OpenGL display is unavailable: " << error.what();
    }
    auto* window = windows.GetWindow(windowHandle);
    ASSERT_NE(window, nullptr);

    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic)) << diagnostic;
    auto device = backend.CreateDevice({});
    const auto swapchain = device->CreateSwapchain(*window, {});

    constexpr std::string_view vertexSource = R"(#version 450
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(position, 1.0); }
)";
    constexpr std::string_view fragmentSource = R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0); }
)";
    const auto vertex = ShaderCompiler::Compile(vertexSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
    const auto fragment = ShaderCompiler::Compile(fragmentSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Fragment, "main"});
    const auto vertexModule = device->CreateShaderModule(vertex);
    const auto fragmentModule = device->CreateShaderModule(fragment);
    const auto pipeline = device->CreatePipeline({
        .vertexShader = vertexModule,
        .fragmentShader = fragmentModule
    });

    device->Destroy(pipeline);
    device->Destroy(vertexModule);
    device->Destroy(fragmentModule);
    EXPECT_TRUE(device->DestroySwapchain(swapchain));
    device.reset();
    backend.Shutdown();
    windows.DestroyWindow(windowHandle);
}

} // namespace
