#include "OpenGLShaderTranslator.hpp"

#include <spirv_cross/spirv_glsl.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Lighthouse::Detail {

std::string TranslateShaderToOpenGL(std::span<const std::uint32_t> spirv,
    ShaderStageFlags stage, std::string_view entryPoint, OpenGLBindingLimits limits) {
    if (spirv.empty() || spirv.front() != 0x07230203u)
        throw std::invalid_argument("Shader module input must contain SPIR-V words");
    if (entryPoint.empty())
        throw std::invalid_argument("Shader entry point cannot be empty");
    try {
        spirv_cross::CompilerGLSL compiler(spirv.data(), spirv.size());
        const auto model = stage == ShaderStageFlags::Vertex ? spv::ExecutionModelVertex :
            stage == ShaderStageFlags::Fragment ? spv::ExecutionModelFragment : spv::ExecutionModelGLCompute;
        compiler.set_entry_point(std::string(entryPoint), model);
        compiler.rename_entry_point(std::string(entryPoint), "main", model);

        const auto resources = compiler.get_shader_resources();
        auto remap = [&compiler](const auto& list, std::uint32_t limit, std::uint32_t& nextBinding) {
            if (nextBinding > limit || list.size() > limit - nextBinding)
                throw std::runtime_error("Shader resource bindings exceed the active OpenGL limit");
            for (const auto& resource : list) {
                compiler.set_decoration(resource.id, spv::DecorationBinding, nextBinding++);
                compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
            }
        };
        std::uint32_t uniformBinding{}, storageBinding{}, textureBinding{}, imageBinding{}, atomicBinding{};
        remap(resources.uniform_buffers, limits.uniformBuffers, uniformBinding);
        remap(resources.push_constant_buffers, limits.uniformBuffers, uniformBinding);
        remap(resources.storage_buffers, limits.storageBuffers, storageBinding);
        remap(resources.sampled_images, limits.textureUnits, textureBinding);
        remap(resources.separate_images, limits.textureUnits, textureBinding);
        remap(resources.separate_samplers, limits.textureUnits, textureBinding);
        remap(resources.subpass_inputs, limits.textureUnits, textureBinding);
        remap(resources.storage_images, limits.imageUnits, imageBinding);
        remap(resources.atomic_counters, limits.atomicCounterBuffers, atomicBinding);
        spirv_cross::CompilerGLSL::Options options;
        options.version = 450;
        options.es = false;
        options.vulkan_semantics = false;
        compiler.set_common_options(options);
        return compiler.compile();
    } catch (const spirv_cross::CompilerError& error) {
        throw std::runtime_error("SPIRV-Cross OpenGL translation failed: " + std::string(error.what()));
    }
}

} // namespace Lighthouse::Detail
