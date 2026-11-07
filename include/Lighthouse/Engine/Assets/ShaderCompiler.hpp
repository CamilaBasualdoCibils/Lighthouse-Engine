#pragma once

#include <string>
#include <string_view>

#include <Lighthouse/Engine/Assets/Shader.hpp>

namespace Lighthouse {

enum class ShaderSourceLanguage { GLSL, HLSL };

struct ShaderCompileOptions {
    ShaderSourceLanguage language{ShaderSourceLanguage::GLSL};
    ShaderStage stage{ShaderStage::Vertex};
    std::string entryPoint{"main"};
};

class ShaderCompiler {
public:
    // Compiles to Vulkan 1.1 / SPIR-V 1.3; ray stages use Vulkan 1.2 / SPIR-V 1.4.
    static Shader Compile(std::string_view source, const ShaderCompileOptions& options);
    // Internal backend path: compiles translated GLSL for OpenGL SPIR-V ingestion.
    static std::vector<std::uint32_t> CompileOpenGL(std::string_view source, ShaderStage stage);
};

} // namespace Lighthouse
