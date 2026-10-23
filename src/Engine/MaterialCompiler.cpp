#include <Lighthouse/Engine/Assets/Assets.hpp>

#include <BuiltinShaderSources.hpp>
#include <slang-com-ptr.h>
#include <slang.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Lighthouse::Assets {
namespace {

std::mutex& CompilerMutex() {
    static std::mutex mutex;
    return mutex;
}

struct CompilerState {
    Slang::ComPtr<slang::IGlobalSession> global;
    Slang::ComPtr<slang::ISession> session;
    Slang::ComPtr<slang::IModule> materialContract;
    Slang::ComPtr<slang::IModule> litWrapper;
    Slang::ComPtr<slang::IModule> unlitWrapper;

    CompilerState() {
        if (SLANG_FAILED(slang::createGlobalSession(global.writeRef())))
            throw std::runtime_error("Failed to create the Slang global session");

        slang::TargetDesc target{};
        target.format = SLANG_SPIRV;
        target.profile = global->findProfile("spirv_1_3");
        slang::CompilerOptionEntry option{};
        option.name = slang::CompilerOptionName::VulkanUseEntryPointName;
        option.value.kind = slang::CompilerOptionValueKind::Int;
        option.value.intValue0 = 1;
        target.compilerOptionEntries = &option;
        target.compilerOptionEntryCount = 1;

        slang::SessionDesc desc{};
        desc.targets = &target;
        desc.targetCount = 1;
        if (SLANG_FAILED(global->createSession(desc, session.writeRef())))
            throw std::runtime_error("Failed to create the Slang Vulkan/SPIR-V session");

        materialContract = LoadModule("LighthouseMaterial", Detail::MaterialContract,
                                      "<LighthouseMaterial.slang>");
        litWrapper = LoadModule("LighthouseLit", Detail::LitMaterial,
                                "<LighthouseLit.slang>");
        unlitWrapper = LoadModule("LighthouseUnlit", Detail::UnlitMaterial,
                                  "<LighthouseUnlit.slang>");
    }

    Slang::ComPtr<slang::IModule> LoadModule(std::string_view name,
                                              std::string_view source,
                                              std::string_view path) {
        Slang::ComPtr<slang::IBlob> diagnostics;
        Slang::ComPtr<slang::IModule> module(session->loadModuleFromSourceString(
            std::string(name).c_str(), std::string(path).c_str(), std::string(source).c_str(),
            diagnostics.writeRef()));
        if (!module)
            throw std::runtime_error("Slang failed to load built-in module '" +
                                     std::string(name) + "': " + BlobText(diagnostics));
        return module;
    }

    static std::string BlobText(const Slang::ComPtr<slang::IBlob>& blob) {
        if (!blob || !blob->getBufferPointer() || !blob->getBufferSize())
            return "no compiler diagnostics were provided";
        return std::string(static_cast<const char*>(blob->getBufferPointer()),
                           blob->getBufferSize());
    }
};

CompilerState& State() {
    static CompilerState state;
    return state;
}

std::string MakeModuleIdentifier(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal().generic_string();
    std::string result = "LighthouseUser_";
    constexpr char digits[] = "0123456789abcdef";
    const auto hash = std::hash<std::string>{}(absolute);
    for (int shift = static_cast<int>(sizeof(hash) * 8) - 4; shift >= 0; shift -= 4)
        result.push_back(digits[(hash >> shift) & 0xf]);
    return result;
}

bool IsTypeIdentifier(std::string_view name) {
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_'))
        return false;
    return std::all_of(name.begin() + 1, name.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '_';
    });
}

std::string Replace(std::string source, std::string_view needle, std::string_view value) {
    const auto position = source.find(needle);
    if (position == std::string::npos)
        throw std::logic_error("Built-in material wrapper is missing a generation marker");
    source.replace(position, needle.size(), value);
    return source;
}

std::string EntrySource(std::string_view source, std::string_view wrapperModule,
                        std::string_view userModule, std::string_view wrapperType,
                        std::string_view materialType) {
    std::string generated(source);
    generated = Replace(std::move(generated), "@LIGHTHOUSE_WRAPPER_MODULE@", wrapperModule);
    generated = Replace(std::move(generated), "@LIGHTHOUSE_USER_MODULE@", userModule);
    generated = Replace(std::move(generated), "@LIGHTHOUSE_SELECTED_WRAPPER@", wrapperType);
    return Replace(std::move(generated), "@LIGHTHOUSE_SELECTED_TYPE@", materialType);
}

std::string DiagnosticsText(const Slang::ComPtr<slang::IBlob>& diagnostics) {
    return CompilerState::BlobText(diagnostics);
}

slang::IModule* LoadUserModule(CompilerState& state, const std::filesystem::path& path,
                               std::string_view moduleName, std::string_view source,
                               Slang::ComPtr<slang::IModule>& ownedModule) {
    Slang::ComPtr<slang::IBlob> diagnostics;
    const auto fullPath = std::filesystem::absolute(path).lexically_normal().string();
    ownedModule = state.session->loadModuleFromSourceString(
        std::string(moduleName).c_str(), fullPath.c_str(), std::string(source).c_str(),
        diagnostics.writeRef());
    if (!ownedModule)
        throw std::runtime_error("Slang failed to compile material source '" + path.string() +
                                 "':\n" + DiagnosticsText(diagnostics));
    return ownedModule.get();
}

bool HasType(slang::IModule* module, std::string_view typeName) {
    auto* reflection = module->getModuleReflection();
    for (unsigned int index = 0; index < reflection->getChildrenCount(); ++index) {
        auto* child = reflection->getChild(index);
        if (child->getKind() == slang::DeclReflection::Kind::Struct && child->getName() &&
            typeName == child->getName())
            return true;
    }
    return false;
}

struct CompileResult {
    bool success{};
    std::optional<Shader> vertex;
    std::optional<Shader> fragment;
    std::string diagnostics;
};

Shader ExtractShader(slang::IComponentType* linked, SlangInt entryPointIndex,
                     ShaderStage stage, const char* entryPoint) {
    Slang::ComPtr<slang::IBlob> code;
    Slang::ComPtr<slang::IBlob> diagnostics;
    const auto result = linked->getEntryPointCode(entryPointIndex, 0, code.writeRef(),
                                                   diagnostics.writeRef());
    if (SLANG_FAILED(result))
        throw std::runtime_error("Slang SPIR-V generation failed for " +
                                 std::string(entryPoint) + ":\n" + DiagnosticsText(diagnostics));
    if (!code || code->getBufferSize() == 0 || code->getBufferSize() % sizeof(std::uint32_t))
        throw std::runtime_error("Slang returned malformed SPIR-V for " + std::string(entryPoint));
    const auto* words = static_cast<const std::uint32_t*>(code->getBufferPointer());
    std::vector<std::uint32_t> spirv(words, words + code->getBufferSize() / sizeof(std::uint32_t));
    return Shader(stage, entryPoint, std::move(spirv));
}

CompileResult TryCompile(CompilerState& state, std::string_view wrapperModuleName,
                         std::string_view wrapperType, std::string_view userModuleName,
                         std::string_view materialType, std::string_view entryTemplate,
                         const std::filesystem::path& sourcePath, bool unlit) {
    CompileResult result;
    auto source = EntrySource(entryTemplate, wrapperModuleName, userModuleName, wrapperType,
                              materialType);
    const auto generatedModuleName = MakeModuleIdentifier(sourcePath) +
        (unlit ? "_UnlitEntry" : "_LitEntry");
    const auto generatedPath = "<" + generatedModuleName + ".slang>";
    Slang::ComPtr<slang::IBlob> diagnostics;
    Slang::ComPtr<slang::IModule> entryModule(state.session->loadModuleFromSourceString(
        generatedModuleName.c_str(), generatedPath.c_str(), source.c_str(), diagnostics.writeRef()));
    if (!entryModule) {
        result.diagnostics = DiagnosticsText(diagnostics);
        return result;
    }

    Slang::ComPtr<slang::IEntryPoint> vertexEntry;
    Slang::ComPtr<slang::IEntryPoint> fragmentEntry;
    if (SLANG_FAILED(entryModule->findEntryPointByName("LighthouseVertex", vertexEntry.writeRef())) ||
        SLANG_FAILED(entryModule->findEntryPointByName("LighthouseFragment", fragmentEntry.writeRef()))) {
        result.diagnostics = "Generated material module is missing a vertex or fragment entry point";
        return result;
    }

    std::array<slang::IComponentType*, 3> components{
        entryModule.get(), vertexEntry.get(), fragmentEntry.get()};
    Slang::ComPtr<slang::IComponentType> composite;
    if (SLANG_FAILED(state.session->createCompositeComponentType(
            components.data(), static_cast<SlangInt>(components.size()), composite.writeRef(),
            diagnostics.writeRef()))) {
        result.diagnostics = DiagnosticsText(diagnostics);
        return result;
    }
    Slang::ComPtr<slang::IComponentType> linked;
    if (SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef()))) {
        result.diagnostics = DiagnosticsText(diagnostics);
        return result;
    }

    try {
        result.vertex.emplace(ExtractShader(linked.get(), 0, ShaderStage::Vertex, "LighthouseVertex"));
        result.fragment.emplace(ExtractShader(linked.get(), 1, ShaderStage::Fragment, "LighthouseFragment"));
        result.success = true;
    } catch (const std::exception& error) {
        result.diagnostics = error.what();
    }
    return result;
}

} // namespace

Material LoadMaterial(const std::filesystem::path& path, std::string_view name) {
    if (!IsTypeIdentifier(name))
        throw std::invalid_argument("LoadMaterial requires a Slang type identifier as its name");
    if (path.extension() != ".slang")
        throw std::invalid_argument("LoadMaterial requires a .slang source file");

    std::lock_guard lock(CompilerMutex());
    auto& state = State();
    const auto source = LoadText(path);
    const auto userModuleName = MakeModuleIdentifier(path);
    Slang::ComPtr<slang::IModule> userModule;
    auto* module = LoadUserModule(state, path, userModuleName, source, userModule);
    if (!HasType(module, name))
        throw std::invalid_argument("Material type '" + std::string(name) +
                                    "' was not found in '" + path.string() + "'");

    // Slang reflection exposes declared members, but not a direct query for an interface's
    // associated type binding. The wrappers constrain T.Base to each supported marker; trying
    // those compiler-checked specializations selects the base without inspecting source text.
    auto lit = TryCompile(state, "LighthouseLit", "LitMaterial", userModuleName, name,
                          Detail::LitEntryTemplate, path, false);
    if (lit.success)
        return Material{std::move(*lit.vertex), std::move(*lit.fragment)};

    auto unlit = TryCompile(state, "LighthouseUnlit", "UnlitMaterial", userModuleName,
                            name, Detail::UnlitEntryTemplate, path, true);
    if (unlit.success)
        return Material{std::move(*unlit.vertex), std::move(*unlit.fragment)};

    if (lit.diagnostics.find("IMaterialDefinition") != std::string::npos ||
        unlit.diagnostics.find("IMaterialDefinition") != std::string::npos)
        throw std::invalid_argument("Material type '" + std::string(name) +
            "' does not satisfy IMaterialDefinition; Slang diagnostics:\n" + lit.diagnostics +
            "\n" + unlit.diagnostics);
    throw std::invalid_argument("Material type '" + std::string(name) +
        "' has an unsupported IMaterialDefinition.Base; expected LitBase or UnlitBase. "
        "Slang diagnostics:\n" + lit.diagnostics + "\n" + unlit.diagnostics);
}

} // namespace Lighthouse::Assets
