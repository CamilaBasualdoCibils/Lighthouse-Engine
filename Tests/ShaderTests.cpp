#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
using namespace Lighthouse;

constexpr std::string_view VertexSource = R"(#version 450
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(position, 1.0); }
)";

constexpr std::string_view FragmentSource = R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0); }
)";

TEST(ShaderTests, CompilesGLSLVertexAndFragmentToSPIRV) {
    const auto vertex = ShaderCompiler::Compile(VertexSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
    const auto fragment = ShaderCompiler::Compile(FragmentSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Fragment, "main"});

    ASSERT_FALSE(vertex.GetSPIRV().empty());
    ASSERT_FALSE(fragment.GetSPIRV().empty());
    EXPECT_EQ(vertex.GetSPIRV().front(), 0x07230203u);
    EXPECT_EQ(fragment.GetSPIRV().front(), 0x07230203u);
    EXPECT_EQ(vertex.GetStage(), ShaderStage::Vertex);
    EXPECT_EQ(fragment.GetStage(), ShaderStage::Fragment);
    EXPECT_EQ(vertex.GetEntryPoint(), "main");
}

TEST(ShaderTests, LoadsShaderFromRawSourceString) {
    const auto shader = Assets::LoadShader(VertexSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
    EXPECT_EQ(shader.GetStage(), ShaderStage::Vertex);
    EXPECT_EQ(shader.GetSPIRV().front(), 0x07230203u);
}

TEST(ShaderTests, CompilesComputeStage) {
    constexpr std::string_view source = R"(#version 450
layout(local_size_x = 1) in;
void main() {}
)";
    const auto shader = ShaderCompiler::Compile(source,
        {ShaderSourceLanguage::GLSL, ShaderStage::Compute, "main"});
    EXPECT_EQ(shader.GetStage(), ShaderStage::Compute);
    ASSERT_FALSE(shader.GetSPIRV().empty());
    EXPECT_EQ(shader.GetSPIRV().front(), 0x07230203u);
}

TEST(ShaderTests, CompilesHLSLWithExplicitEntryPoint) {
    constexpr std::string_view source =
        "float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }";
    const auto shader = ShaderCompiler::Compile(source,
        {ShaderSourceLanguage::HLSL, ShaderStage::Vertex, "VSMain"});

    EXPECT_EQ(shader.GetStage(), ShaderStage::Vertex);
    EXPECT_EQ(shader.GetEntryPoint(), "VSMain");
    ASSERT_FALSE(shader.GetSPIRV().empty());
    EXPECT_EQ(shader.GetSPIRV().front(), 0x07230203u);
}

TEST(ShaderTests, InvalidSourceReportsCompilationDiagnostics) {
    try {
        (void)ShaderCompiler::Compile("#version 450\nvoid main( {", 
            {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
        FAIL() << "Expected invalid shader source to fail";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("Shader compilation failed"), std::string::npos);
        EXPECT_GT(std::string(error.what()).size(), 30u);
    }
}

TEST(ShaderTests, LoadsTextAndInfersGLSLStage) {
    const auto path = std::filesystem::temp_directory_path() /
        ("lighthouse-shader-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".vert");
    {
        std::ofstream file(path, std::ios::binary);
        file << VertexSource;
    }
    EXPECT_EQ(Assets::LoadText(path), VertexSource);
    const auto shader = Assets::LoadShader(path);
    EXPECT_EQ(shader.GetStage(), ShaderStage::Vertex);
    EXPECT_EQ(shader.GetSPIRV().front(), 0x07230203u);
    std::filesystem::remove(path);
}

TEST(ShaderTests, HLSLRequiresExplicitStageAndAllowsOverrides) {
    const auto path = std::filesystem::temp_directory_path() /
        ("lighthouse-shader-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".hlsl");
    {
        std::ofstream file(path);
        file << "float4 CSMain() : SV_Target { return float4(1, 0, 0, 1); }";
    }
    EXPECT_THROW(Assets::LoadShader(path), std::invalid_argument);
    const auto shader = Assets::LoadShader(path, {
        .language = ShaderSourceLanguage::HLSL,
        .stage = ShaderStage::Fragment,
        .entryPoint = "CSMain"
    });
    EXPECT_EQ(shader.GetStage(), ShaderStage::Fragment);
    EXPECT_EQ(shader.GetEntryPoint(), "CSMain");
    std::filesystem::remove(path);
}

} // namespace
