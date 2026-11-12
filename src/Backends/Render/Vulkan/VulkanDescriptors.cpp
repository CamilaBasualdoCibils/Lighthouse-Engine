#include "VulkanDeviceState.hpp"
namespace Lighthouse
{
ShaderModuleHandle
VulkanRenderDevice::CreateShaderModule(const ShaderModuleDesc& desc)
{
  Require(!desc.bytecode.empty() && desc.bytecode.size() % 4 == 0 &&
              !desc.entryPoint.empty(),
          "Invalid SPIR-V shader");
  auto& d = *m_impl;
  Impl::Module module;
  std::vector<uint32_t> words(desc.bytecode.size() / sizeof(uint32_t));
  std::memcpy(words.data(), desc.bytecode.data(), desc.bytecode.size());
  module.native = d.device.createShaderModule(
      vk::ShaderModuleCreateInfo({}, desc.bytecode.size(), words.data()),
      nullptr, d.dispatch);
  module.stage = desc.stage;
  module.entry = desc.entryPoint;
  Stage(desc.stage);
  auto h = d.Handle<ShaderModuleHandle>(d.nextModule);
  d.modules.emplace(h, std::move(module));
  return h;
}
void VulkanRenderDevice::DestroyModule(ShaderModuleHandle h)
{
  m_impl->device.destroyShaderModule(m_impl->modules.at(h).native, nullptr,
                                     m_impl->dispatch);
  m_impl->modules.erase(h);
}
DescriptorSetLayoutHandle VulkanRenderDevice::CreateDescriptorSetLayout(
    const DescriptorSetLayoutDesc& desc)
{
  auto& d = *m_impl;
  Impl::SetLayout layout;
  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  std::vector<vk::DescriptorBindingFlags> flags;
  for (const auto& b : desc.bindings)
  {
    Require(b.count && static_cast<unsigned>(b.stages),
            "Invalid descriptor binding");
    Require(layout.bindings.emplace(b.binding, b).second,
            "Duplicate descriptor binding");
    bindings.emplace_back(b.binding, Descriptor(b.type), b.count,
                          Stages(b.stages));
    flags.push_back(d.updateAfterBind.at(static_cast<unsigned>(b.type))
                        ? vk::DescriptorBindingFlagBits::eUpdateAfterBind
                        : vk::DescriptorBindingFlags{});
  }
  vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlags;
  bindingFlags.setBindingFlags(flags);
  vk::DescriptorSetLayoutCreateInfo info(
      vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool, bindings);
  info.setPNext(&bindingFlags);
  layout.native = d.device.createDescriptorSetLayout(info, nullptr, d.dispatch);
  auto h = d.Handle<DescriptorSetLayoutHandle>(d.nextSetLayout);
  d.setLayouts.emplace(h, std::move(layout));
  return h;
}
DescriptorSetHandle
VulkanRenderDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
{
  auto& d = *m_impl;
  auto& l = d.setLayouts.at(desc.layout);
  auto native = d.device
                    .allocateDescriptorSets(vk::DescriptorSetAllocateInfo(
                                                d.descriptorPool, l.native),
                                            d.dispatch)
                    .front();
  auto h = d.Handle<DescriptorSetHandle>(d.nextSet);
  d.sets.emplace(h, Impl::Set{native, desc.layout});
  return h;
}
PipelineLayoutHandle
VulkanRenderDevice::CreatePipelineLayout(const PipelineLayoutDesc& desc)
{
  auto& d = *m_impl;
  std::vector<vk::DescriptorSetLayout> sets;
  for (auto h : desc.sets)
    sets.push_back(d.setLayouts.at(h).native);
  vk::PushConstantRange drawIndex(vk::ShaderStageFlagBits::eVertex |
                                      vk::ShaderStageFlagBits::eFragment,
                                  0, sizeof(uint32_t));
  vk::PipelineLayoutCreateInfo info({}, sets);
  if (desc.drawIndex)
    info.setPushConstantRanges(drawIndex);
  auto native = d.device.createPipelineLayout(info, nullptr, d.dispatch);
  auto h = d.Handle<PipelineLayoutHandle>(d.nextLayout);
  d.layouts.emplace(h, Impl::Layout{native});
  return h;
}
SamplerHandle VulkanRenderDevice::CreateSampler(const SamplerDesc& desc)
{
  auto& d = *m_impl;
  auto address = [](SamplerAddressMode m)
  {
    switch (m)
    {
    case SamplerAddressMode::Repeat:
      return vk::SamplerAddressMode::eRepeat;
    case SamplerAddressMode::MirroredRepeat:
      return vk::SamplerAddressMode::eMirroredRepeat;
    case SamplerAddressMode::ClampToEdge:
      return vk::SamplerAddressMode::eClampToEdge;
    case SamplerAddressMode::ClampToBorder:
      return vk::SamplerAddressMode::eClampToBorder;
    }
    throw std::invalid_argument("Unknown sampler address mode");
  };
  vk::SamplerCreateInfo info;
  info.setMagFilter(desc.magnification == SamplerFilter::Linear
                        ? vk::Filter::eLinear
                        : vk::Filter::eNearest)
      .setMinFilter(desc.minification == SamplerFilter::Linear
                        ? vk::Filter::eLinear
                        : vk::Filter::eNearest)
      .setMipmapMode(desc.mipmap == SamplerMipmapFilter::Linear
                         ? vk::SamplerMipmapMode::eLinear
                         : vk::SamplerMipmapMode::eNearest)
      .setAddressModeU(address(desc.addressU))
      .setAddressModeV(address(desc.addressV))
      .setAddressModeW(address(desc.addressW))
      .setMaxLod(desc.mipmap == SamplerMipmapFilter::None ? 0
                                                          : vk::LodClampNone)
      .setBorderColor(vk::BorderColor::eFloatTransparentBlack);
  Require(!desc.anisotropy ||
              d.physical.getFeatures(d.dispatch).samplerAnisotropy,
          "Anisotropic filtering unavailable");
  info.setAnisotropyEnable(desc.anisotropy)
      .setMaxAnisotropy(std::clamp(desc.maxAnisotropy, 1.0f,
                                   d.properties.limits.maxSamplerAnisotropy));
  auto h = d.Handle<SamplerHandle>(d.nextSampler);
  d.samplers.emplace(h, d.device.createSampler(info, nullptr, d.dispatch));
  return h;
}
void VulkanRenderDevice::UpdateDescriptorSet(
    DescriptorSetHandle h, std::span<const DescriptorWrite> writes)
{
  auto& d = *m_impl;
  auto& set = d.sets.at(h);
  const auto& layout = d.setLayouts.at(set.layout);
  for (const auto& write : writes)
  {
    const auto& binding = layout.bindings.at(write.binding);
    Require(write.arrayElement < binding.count,
            "Descriptor array out of range");
    vk::WriteDescriptorSet native;
    native.setDstSet(set.native)
        .setDstBinding(write.binding)
        .setDstArrayElement(write.arrayElement)
        .setDescriptorType(Descriptor(binding.type))
        .setDescriptorCount(1);
    vk::DescriptorBufferInfo bufferInfo;
    vk::DescriptorImageInfo imageInfo;
    if (binding.type == DescriptorType::CombinedImageSampler)
    {
      const auto& info = std::get<DescriptorImageInfo>(write.resource);
      const auto& view = d.views.at(info.image);
      auto& image = d.images.at(view.desc.image);
      Require(image.desc.usage.Contains(ImageUsage::Sampled),
              "Descriptor image requires Sampled usage");
      // The caller supplies the layout and records its image barrier
      // explicitly.
      imageInfo =
          vk::DescriptorImageInfo(d.samplers.at(info.sampler), view.native,
                                  static_cast<vk::ImageLayout>(info.layout));
      native.setPImageInfo(&imageInfo);
    }
    else
    {
      const auto& info = std::get<DescriptorBufferInfo>(write.resource);
      const auto& buffer = d.buffers.at(info.buffer);
      const auto required = binding.type == DescriptorType::UniformBuffer
                                ? BufferUsage::Uniform
                                : BufferUsage::Storage;
      Require(buffer.desc.usage.Contains(required),
              "Buffer descriptor usage mismatch");
      Require(info.offset < buffer.desc.size,
              "Descriptor offset exceeds buffer");
      const auto range = info.range == WholeBuffer
                             ? buffer.desc.size - info.offset
                             : info.range;
      Require(range && range <= buffer.desc.size - info.offset,
              "Descriptor range exceeds buffer");
      const auto alignment =
          binding.type == DescriptorType::UniformBuffer
              ? d.properties.limits.minUniformBufferOffsetAlignment
              : d.properties.limits.minStorageBufferOffsetAlignment;
      Require(info.offset % alignment == 0,
              "Descriptor buffer offset alignment");
      bufferInfo = vk::DescriptorBufferInfo(buffer.native, info.offset, range);
      native.setPBufferInfo(&bufferInfo);
    }
    d.device.updateDescriptorSets(native, {}, d.dispatch);
  }
}
GraphicsPipelineHandle
VulkanRenderDevice::CreatePipeline(const GraphicsPipelineDesc& desc)
{
  auto& d = *m_impl;
  auto& layout = d.layouts.at(desc.layout);
  Impl::Pipeline pipeline;
  pipeline.layout = desc.layout;
  pipeline.compute = static_cast<bool>(desc.computeShader);
  const auto stage =
      [&](ShaderModuleHandle handle, vk::ShaderStageFlagBits stage)
  {
    const auto& module = d.modules.at(handle);
    return vk::PipelineShaderStageCreateInfo({}, stage, module.native,
                                             module.entry.c_str());
  };
  if (pipeline.compute)
  {
    pipeline.native =
        d.device
            .createComputePipeline({},
                                   vk::ComputePipelineCreateInfo(
                                       {},
                                       stage(desc.computeShader,
                                             vk::ShaderStageFlagBits::eCompute),
                                       layout.native),
                                   nullptr, d.dispatch)
            .value;
  }
  else
  {
    std::vector<vk::VertexInputBindingDescription> bindings;
    std::vector<vk::VertexInputAttributeDescription> attributes;
    for (const auto& binding : desc.vertexLayout.bindings)
      bindings.emplace_back(binding.binding, binding.stride,
                            binding.inputRate == VertexInputRate::PerInstance
                                ? vk::VertexInputRate::eInstance
                                : vk::VertexInputRate::eVertex);
    for (const auto& attribute : desc.vertexLayout.attributes)
      attributes.emplace_back(attribute.location, attribute.binding,
                              VertexFormatNative(attribute.format),
                              attribute.offset);
    vk::PipelineVertexInputStateCreateInfo vertex({}, bindings, attributes);
    vk::PipelineInputAssemblyStateCreateInfo assembly({},
                                                      Topology(desc.topology));
    vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
    vk::PipelineRasterizationStateCreateInfo raster(
        {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone,
        vk::FrontFace::eCounterClockwise, false, 0, 0, 0, 1);
    vk::PipelineMultisampleStateCreateInfo samples({},
                                                   Samples(desc.sampleCount));
    const auto mode = desc.depthStencil.stencil;
    vk::StencilOpState stencil(
        vk::StencilOp::eKeep,
        mode == StencilMode::Write ? vk::StencilOp::eReplace
                                   : vk::StencilOp::eKeep,
        vk::StencilOp::eKeep,
        mode == StencilMode::Equal ? vk::CompareOp::eEqual
                                   : vk::CompareOp::eAlways,
        0xff, mode == StencilMode::Write ? 0xff : 0, 0);
    const bool hasDepth = desc.depthFormat != ImageFormat::Undefined;
    vk::PipelineDepthStencilStateCreateInfo depth(
        {}, desc.depthStencil.depthTest,
        desc.depthStencil.depthWrite,
        static_cast<vk::CompareOp>(desc.depthStencil.depthCompare), false,
        mode != StencilMode::Disabled, stencil, stencil);
    const auto& b = desc.blend;
    vk::PipelineColorBlendAttachmentState blend(
        b.enabled, BlendFactorNative(b.sourceColorFactor),
        BlendFactorNative(b.destinationColorFactor),
        BlendOperationNative(b.colorOperation),
        BlendFactorNative(b.sourceAlphaFactor),
        BlendFactorNative(b.destinationAlphaFactor),
        BlendOperationNative(b.alphaOperation),
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    std::vector<vk::PipelineColorBlendAttachmentState> blends(
        desc.colorFormats.size(), blend);
    vk::PipelineColorBlendStateCreateInfo color({}, false, vk::LogicOp::eCopy,
                                                blends);
    color.blendConstants = std::array<float, 4>{b.constants.x, b.constants.y,
                                                b.constants.z, b.constants.w};
    const std::array dynamicStates{vk::DynamicState::eViewport,
                                   vk::DynamicState::eScissor,
                                   vk::DynamicState::eStencilReference};
    vk::PipelineDynamicStateCreateInfo dynamic({}, dynamicStates);
    const std::array stages{
        stage(desc.vertexShader, vk::ShaderStageFlagBits::eVertex),
        stage(desc.fragmentShader, vk::ShaderStageFlagBits::eFragment)};
    std::vector<vk::Format> formats;
    for (auto format : desc.colorFormats)
      formats.push_back(Format(format));
    vk::PipelineRenderingCreateInfo rendering(
        desc.viewMask, formats,
        hasDepth ? Format(desc.depthFormat) : vk::Format::eUndefined,
        desc.stencilFormat==ImageFormat::Undefined ? vk::Format::eUndefined : Format(desc.stencilFormat));
    vk::GraphicsPipelineCreateInfo info;
    info.setStages(stages)
        .setPVertexInputState(&vertex)
        .setPInputAssemblyState(&assembly)
        .setPViewportState(&viewport)
        .setPRasterizationState(&raster)
        .setPMultisampleState(&samples)
        .setPDepthStencilState(&depth)
        .setPColorBlendState(&color)
        .setPDynamicState(&dynamic)
        .setLayout(layout.native)
        .setPNext(&rendering);
    pipeline.native =
        d.device.createGraphicsPipeline({}, info, nullptr, d.dispatch).value;
  }
  const auto handle = d.Handle<GraphicsPipelineHandle>(d.nextPipeline);
  try
  {
    d.pipelines.emplace(handle, pipeline);
  }
  catch (...)
  {
    d.device.destroyPipeline(pipeline.native, nullptr, d.dispatch);
    throw;
  }
  return handle;
}
void VulkanRenderDevice::DestroyPipeline(GraphicsPipelineHandle handle)
{
  auto& d = *m_impl;
  d.device.destroyPipeline(d.pipelines.at(handle).native, nullptr, d.dispatch);
  d.pipelines.erase(handle);
}
void VulkanRenderDevice::DestroyDescriptorSet(DescriptorSetHandle h)
{
  auto& d = *m_impl;
  auto it = d.sets.find(h);
  if (it == d.sets.end())
    return;
  d.device.freeDescriptorSets(d.descriptorPool, it->second.native, d.dispatch);
  d.sets.erase(it);
}
void VulkanRenderDevice::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle h)
{
  auto& d = *m_impl;
  auto it = d.setLayouts.find(h);
  if (it == d.setLayouts.end())
    return;
  d.device.destroyDescriptorSetLayout(it->second.native, nullptr, d.dispatch);
  d.setLayouts.erase(it);
}
void VulkanRenderDevice::DestroyPipelineLayout(PipelineLayoutHandle h)
{
  auto& d = *m_impl;
  auto it = d.layouts.find(h);
  if (it == d.layouts.end())
    return;
  d.device.destroyPipelineLayout(it->second.native, nullptr, d.dispatch);
  d.layouts.erase(it);
}
void VulkanRenderDevice::DestroySampler(SamplerHandle h)
{
  auto& d = *m_impl;
  auto it = d.samplers.find(h);
  if (it == d.samplers.end())
    return;
  d.device.destroySampler(it->second, nullptr, d.dispatch);
  d.samplers.erase(it);
}
bool VulkanRenderDevice::IsValid(DescriptorSetLayoutHandle h) const noexcept
{
  return m_impl->setLayouts.contains(h);
}
bool VulkanRenderDevice::IsValid(DescriptorSetHandle h) const noexcept
{
  return m_impl->sets.contains(h);
}
bool VulkanRenderDevice::IsValid(PipelineLayoutHandle h) const noexcept
{
  return m_impl->layouts.contains(h);
}
bool VulkanRenderDevice::IsValid(SamplerHandle h) const noexcept
{
  return m_impl->samplers.contains(h);
}

} // namespace Lighthouse
