#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace Lighthouse::Assets {
std::string LoadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Failed to open text asset '" + path.string() + "'");
    std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad())
        throw std::runtime_error("Failed to read text asset '" + path.string() + "'");
    return source;
}

Shader LoadShader(const std::filesystem::path& path, const ShaderLoadOptions& options) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    ShaderCompileOptions compileOptions;
    if (options.language) {
        compileOptions.language = *options.language;
    } else if (extension == ".hlsl") {
        compileOptions.language = ShaderSourceLanguage::HLSL;
    } else if (extension == ".glsl" || extension == ".vert" || extension == ".frag" ||
               extension == ".comp" || extension == ".vs" || extension == ".fs" || extension == ".cs") {
        compileOptions.language = ShaderSourceLanguage::GLSL;
    } else {
        throw std::invalid_argument("Cannot infer shader source language from asset extension '" + extension + "'");
    }
    compileOptions.entryPoint = options.entryPoint;
    if (options.stage) {
        compileOptions.stage = *options.stage;
    } else {
        if (compileOptions.language == ShaderSourceLanguage::HLSL)
            throw std::invalid_argument("LoadShader requires an explicit stage for HLSL assets");
        if (extension == ".vert" || extension == ".vs") compileOptions.stage = ShaderStage::Vertex;
        else if (extension == ".frag" || extension == ".fs") compileOptions.stage = ShaderStage::Fragment;
        else if (extension == ".comp" || extension == ".cs") compileOptions.stage = ShaderStage::Compute;
        else throw std::invalid_argument("Cannot infer shader stage from asset extension '" + extension + "'");
    }
    return ShaderCompiler::Compile(LoadText(path), compileOptions);
}

Shader LoadShader(std::string_view source, const ShaderCompileOptions& options) {
    return ShaderCompiler::Compile(source, options);
}


}
