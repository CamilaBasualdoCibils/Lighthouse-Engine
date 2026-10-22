#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace Lighthouse {
namespace {

using namespace glslang;

EShLanguage ToGlslangStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex: return EShLangVertex;
    case ShaderStage::Fragment: return EShLangFragment;
    case ShaderStage::Compute: return EShLangCompute;
    }
    throw std::invalid_argument("Unsupported shader stage");
}

class GlslangSession {
public:
    GlslangSession() {
        if (!glslang::InitializeProcess())
            throw std::runtime_error("Failed to initialize glslang");
    }
    ~GlslangSession() { glslang::FinalizeProcess(); }
};

std::mutex& CompilerMutex() {
    static std::mutex mutex;
    return mutex;
}

void EnsureGlslangInitialized() {
    static GlslangSession session;
    (void)session;
}

} // namespace

Shader ShaderCompiler::Compile(std::string_view source, const ShaderCompileOptions& options) {
    EnsureGlslangInitialized();
    std::lock_guard lock(CompilerMutex());
    if (source.empty())
        throw std::invalid_argument("Shader source is empty");
    if (options.entryPoint.empty())
        throw std::invalid_argument("Shader entry point cannot be empty");
    if (options.language == ShaderSourceLanguage::GLSL && options.entryPoint != "main")
        throw std::invalid_argument("GLSL shaders must use the 'main' entry point");

    const auto stage = ToGlslangStage(options.stage);
    const char* sourceData = source.data();
    const int sourceLength = static_cast<int>(source.size());
    glslang::TShader shader(stage);
    shader.setStringsWithLengths(&sourceData, &sourceLength, 1);
    shader.setEntryPoint(options.entryPoint.c_str());
    shader.setSourceEntryPoint(options.entryPoint.c_str());
    const auto language = options.language == ShaderSourceLanguage::HLSL ? EShSourceHlsl : EShSourceGlsl;
    shader.setEnvInput(language, stage, EShClientVulkan, 100);
    shader.setEnvClient(EShClientVulkan, EShTargetVulkan_1_1);
    shader.setEnvTarget(EShTargetSpv, EShTargetSpv_1_3);
    if (options.language == ShaderSourceLanguage::HLSL) {
        shader.setAutoMapBindings(true);
        shader.setAutoMapLocations(true);
    }

    constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
        const std::string diagnostics = std::string(shader.getInfoLog()) + shader.getInfoDebugLog();
        throw std::runtime_error("Shader compilation failed:\n" + diagnostics);
    }
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        const std::string diagnostics = std::string(program.getInfoLog()) + program.getInfoDebugLog();
        throw std::runtime_error("Shader linking failed:\n" + diagnostics);
    }
    const auto* intermediate = program.getIntermediate(stage);
    if (!intermediate)
        throw std::runtime_error("glslang produced no intermediate representation");
    std::vector<std::uint32_t> spirv;
    glslang::SpvOptions spvOptions;
    glslang::GlslangToSpv(*intermediate, spirv, &spvOptions);
    if (spirv.empty())
        throw std::runtime_error("glslang produced empty SPIR-V output");
    return Shader(options.stage, options.entryPoint, std::move(spirv));
}

std::vector<std::uint32_t> ShaderCompiler::CompileOpenGL(std::string_view source, ShaderStage shaderStage) {
    EnsureGlslangInitialized();
    std::lock_guard lock(CompilerMutex());
    const auto stage = ToGlslangStage(shaderStage);
    const char* sourceData = source.data();
    const int sourceLength = static_cast<int>(source.size());
    glslang::TShader shader(stage);
    shader.setStringsWithLengths(&sourceData, &sourceLength, 1);
    shader.setEntryPoint("main");
    shader.setSourceEntryPoint("main");
    shader.setEnvInput(EShSourceGlsl, stage, EShClientOpenGL, 450);
    shader.setEnvClient(EShClientOpenGL, EShTargetOpenGL_450);
    shader.setEnvTarget(EShTargetSpv, EShTargetSpv_1_0);
    constexpr auto messages = static_cast<EShMessages>(EShMsgSpvRules);
    if (!shader.parse(GetDefaultResources(), 450, false, messages))
        throw std::runtime_error("OpenGL SPIR-V shader compilation failed:\n" +
            std::string(shader.getInfoLog()) + shader.getInfoDebugLog());
    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
        throw std::runtime_error("OpenGL SPIR-V shader linking failed:\n" +
            std::string(program.getInfoLog()) + program.getInfoDebugLog());
    const auto* intermediate = program.getIntermediate(stage);
    if (!intermediate)
        throw std::runtime_error("glslang produced no OpenGL intermediate representation");
    std::vector<std::uint32_t> spirv;
    glslang::SpvOptions spvOptions;
    glslang::GlslangToSpv(*intermediate, spirv, &spvOptions);
    if (spirv.empty())
        throw std::runtime_error("glslang produced empty OpenGL SPIR-V output");
    return spirv;
}

} // namespace Lighthouse
