#include "OpenGLShaderTranslator.hpp"

#include <spirv_cross/spirv_glsl.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace Lighthouse::Detail {

std::string TranslateShaderToOpenGL(std::span<const std::uint32_t> spirv,
    ShaderStageFlags stage, std::string_view entryPoint, OpenGLBindingLimits limits,
    const DescriptorBindingLocations* descriptorBindings) {
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
        auto remap = [&compiler, descriptorBindings, stage](const auto& list, DescriptorType type,
                                                     std::uint32_t limit,
                                                     std::uint32_t& nextBinding) {
            if (nextBinding > limit || list.size() > limit - nextBinding)
                throw std::runtime_error("Shader resource bindings exceed the active OpenGL limit");
            for (const auto& resource : list) {
                std::uint32_t targetBinding{};
                if (descriptorBindings) {
                    const auto set = compiler.has_decoration(resource.id, spv::DecorationDescriptorSet)
                        ? compiler.get_decoration(resource.id, spv::DecorationDescriptorSet) : 0u;
                    const auto binding = compiler.has_decoration(resource.id, spv::DecorationBinding)
                        ? compiler.get_decoration(resource.id, spv::DecorationBinding) : 0u;
                    const auto location = descriptorBindings->find({set, binding});
                    if (location == descriptorBindings->end() || location->second.type != type ||
                        (static_cast<std::uint32_t>(location->second.stages) & static_cast<std::uint32_t>(stage)) == 0)
                        throw std::invalid_argument("Shader descriptor does not match the pipeline layout");
                    targetBinding = location->second.bindingPoint;
                    if (targetBinding >= limit || location->second.count > limit - targetBinding)
                        throw std::runtime_error("Pipeline layout exceeds an OpenGL binding-point limit");
                    const auto reflectedType = compiler.get_type(resource.type_id);
                    const auto reflectedCount = reflectedType.array.empty() ? 1u : reflectedType.array.front();
                    if (reflectedCount > location->second.count)
                        throw std::invalid_argument("Shader descriptor array exceeds the pipeline layout count");
                } else {
                    targetBinding = nextBinding++;
                }
                compiler.set_decoration(resource.id, spv::DecorationBinding, targetBinding);
                compiler.unset_decoration(resource.id, spv::DecorationDescriptorSet);
            }
        };
        std::uint32_t uniformBinding{}, storageBinding{}, textureBinding{}, imageBinding{}, atomicBinding{};
        remap(resources.uniform_buffers, DescriptorType::UniformBuffer, limits.uniformBuffers, uniformBinding);
        if (!resources.push_constant_buffers.empty())
            throw std::invalid_argument("Push constants are not supported by the descriptor layout API");
        remap(resources.storage_buffers, DescriptorType::StorageBuffer, limits.storageBuffers, storageBinding);
        remap(resources.sampled_images, DescriptorType::CombinedImageSampler, limits.textureUnits, textureBinding);
        remap(resources.separate_images, DescriptorType::CombinedImageSampler, limits.textureUnits, textureBinding);
        remap(resources.separate_samplers, DescriptorType::CombinedImageSampler, limits.textureUnits, textureBinding);
        remap(resources.subpass_inputs, DescriptorType::CombinedImageSampler, limits.textureUnits, textureBinding);
        if (!resources.storage_images.empty() || !resources.atomic_counters.empty())
            throw std::invalid_argument("Storage images and atomic counters are not supported by the descriptor layout API");
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
