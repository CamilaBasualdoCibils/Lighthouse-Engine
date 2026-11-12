#include "VulkanDeviceState.hpp"
namespace Lighthouse
{
struct VulkanCommandList::Impl
{
  VulkanRenderDevice* owner;
  VulkanRenderDevice::Impl& device;
  vk::CommandBuffer command;
  vk::CommandPool pool;
  vk::PipelineLayout layout;
  vk::PipelineBindPoint bindPoint{vk::PipelineBindPoint::eGraphics};
  Impl(VulkanRenderDevice& owner, unsigned queue)
      : owner(&owner), device(*owner.m_impl)
  {
    const auto family = device.queues.at(queue).family;
    auto& queuePool = device.commandPools[family];
    if (!queuePool)
      queuePool = device.device.createCommandPool(
          vk::CommandPoolCreateInfo(
              vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family),
          nullptr, device.dispatch);
    pool = queuePool;
    command = device.device
                  .allocateCommandBuffers(
                      vk::CommandBufferAllocateInfo(
                          pool, vk::CommandBufferLevel::ePrimary, 1),
                      device.dispatch)
                  .front();
  }
  ~Impl()
  {
    device.device.freeCommandBuffers(pool, command, device.dispatch);
  }
};
VulkanCommandList::VulkanCommandList(VulkanRenderDevice& d, uint32_t q)
    : m_impl(std::make_unique<Impl>(d, q))
{
}
VulkanCommandList::~VulkanCommandList() = default;
const vk::CommandBuffer& VulkanCommandList::Native() const noexcept
{
  return m_impl->command;
}
void VulkanCommandList::Begin()
{
  m_impl->command.begin(vk::CommandBufferBeginInfo{}, m_impl->device.dispatch);
}
void VulkanCommandList::End()
{
  m_impl->command.end(m_impl->device.dispatch);
}
void VulkanCommandList::Clear()
{
  m_impl->command.reset({}, m_impl->device.dispatch);
}
void VulkanCommandList::MemoryBarrier(const MemoryBarrierDesc& desc)
{
  vk::MemoryBarrier2 barrier(
      vk::PipelineStageFlags2(
          static_cast<VkPipelineStageFlags2>(desc.srcStageMask)),
      vk::AccessFlags2(static_cast<VkAccessFlags2>(desc.srcAccessMask)),
      vk::PipelineStageFlags2(
          static_cast<VkPipelineStageFlags2>(desc.dstStageMask)),
      vk::AccessFlags2(static_cast<VkAccessFlags2>(desc.dstAccessMask)));
  vk::DependencyInfo dependency;
  dependency.setMemoryBarriers(barrier);
  m_impl->command.pipelineBarrier2(dependency, m_impl->device.dispatch);
}
void VulkanCommandList::ImageBarrier(const ImageBarrierDesc& desc)
{
  auto& d = m_impl->device;
  vk::ImageMemoryBarrier2 barrier;
  barrier
      .setSrcStageMask(vk::PipelineStageFlags2(
          static_cast<VkPipelineStageFlags2>(desc.srcStageMask)))
      .setSrcAccessMask(
          vk::AccessFlags2(static_cast<VkAccessFlags2>(desc.srcAccessMask)))
      .setDstStageMask(vk::PipelineStageFlags2(
          static_cast<VkPipelineStageFlags2>(desc.dstStageMask)))
      .setDstAccessMask(
          vk::AccessFlags2(static_cast<VkAccessFlags2>(desc.dstAccessMask)))
      .setOldLayout(static_cast<vk::ImageLayout>(desc.oldLayout))
      .setNewLayout(static_cast<vk::ImageLayout>(desc.newLayout))
      .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
      .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
      .setImage(d.images.at(desc.image).native)
      .setSubresourceRange(vk::ImageSubresourceRange(
          Aspect(desc.subresource.aspectMask), desc.subresource.baseMipLevel,
          desc.subresource.mipLevelCount, desc.subresource.baseArrayLayer,
          desc.subresource.layerCount));
  vk::DependencyInfo dependency;
  dependency.setImageMemoryBarriers(barrier);
  m_impl->command.pipelineBarrier2(dependency, d.dispatch);
  // Mirror an explicitly supplied layout for the blocking ReadImage diagnostic.
  d.images.at(desc.image).layout = static_cast<vk::ImageLayout>(desc.newLayout);
}
void VulkanCommandList::BeginRendering(const RenderPassDesc& desc)
{
  auto& d = m_impl->device;
  auto views = desc.colorAttachments;
  if (desc.colorAttachment)
    views.push_back(desc.colorAttachment);
  std::vector<vk::RenderingAttachmentInfo> colors;
  for (auto view : views)
  {
    vk::RenderingAttachmentInfo color;
    color.setImageView(d.views.at(view).native)
        .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
        .setLoadOp(desc.clearColor ? vk::AttachmentLoadOp::eClear
                                   : vk::AttachmentLoadOp::eLoad)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{
            desc.clearColorValue.x, desc.clearColorValue.y,
            desc.clearColorValue.z, desc.clearColorAlpha})));
    colors.push_back(color);
  }
  vk::RenderingAttachmentInfo depth,stencil;
  vk::RenderingInfo info;
  info.setRenderArea(
          vk::Rect2D({desc.offsetX, desc.offsetY}, {desc.width, desc.height}))
      .setLayerCount(desc.layerCount)
      .setViewMask(desc.viewMask)
      .setColorAttachments(colors);
  if (desc.depthAttachment)
  {
    const auto& view = d.views.at(desc.depthAttachment);
    depth.setImageView(view.native)
        .setImageLayout(static_cast<vk::ImageLayout>(desc.depthLayout))
        .setLoadOp(desc.clearDepth ? vk::AttachmentLoadOp::eClear
                                   : vk::AttachmentLoadOp::eLoad)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue(vk::ClearDepthStencilValue(1, 0)));
    info.setPDepthAttachment(&depth);
  }
  if(desc.stencilAttachment) {
    stencil.setImageView(d.views.at(desc.stencilAttachment).native)
        .setImageLayout(static_cast<vk::ImageLayout>(desc.stencilLayout))
        .setLoadOp(desc.clearDepth ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad)
        .setStoreOp(vk::AttachmentStoreOp::eStore)
        .setClearValue(vk::ClearValue(vk::ClearDepthStencilValue(1,0)));
    info.setPStencilAttachment(&stencil);
  }
  m_impl->command.beginRendering(info, d.dispatch);
  m_impl->command.setViewport(
      0,
      vk::Viewport(float(desc.offsetX), float(desc.offsetY + desc.height),
                   float(desc.width), -float(desc.height), 0, 1),
      d.dispatch);
  m_impl->command.setScissor(
      0, vk::Rect2D({desc.offsetX, desc.offsetY}, {desc.width, desc.height}),
      d.dispatch);
}
void VulkanCommandList::EndRendering()
{
  m_impl->command.endRendering(m_impl->device.dispatch);
}
void VulkanCommandList::BindPipeline(GraphicsPipelineHandle handle)
{
  auto& d = m_impl->device;
  const auto& pipeline = d.pipelines.at(handle);
  m_impl->layout = d.layouts.at(pipeline.layout).native;
  m_impl->bindPoint = pipeline.compute ? vk::PipelineBindPoint::eCompute
                                       : vk::PipelineBindPoint::eGraphics;
  m_impl->command.bindPipeline(m_impl->bindPoint, pipeline.native, d.dispatch);
}
void VulkanCommandList::BindDescriptorSet(uint32_t index,
                                          DescriptorSetHandle set)
{
  m_impl->command.bindDescriptorSets(m_impl->bindPoint, m_impl->layout, index,
                                     m_impl->device.sets.at(set).native, {},
                                     m_impl->device.dispatch);
}
void VulkanCommandList::SetDrawIndex(uint32_t index)
{
  m_impl->command.pushConstants(
      m_impl->layout,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
      sizeof(index), &index, m_impl->device.dispatch);
}
void VulkanCommandList::SetStencilReference(uint32_t reference)
{
  m_impl->command.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                      reference, m_impl->device.dispatch);
}
void VulkanCommandList::BindVertexBuffer(BufferHandle buffer, uint32_t)
{
  m_impl->command.bindVertexBuffers(0, m_impl->device.buffers.at(buffer).native,
                                    vk::DeviceSize{0}, m_impl->device.dispatch);
}
void VulkanCommandList::BindIndexBuffer(BufferHandle buffer)
{
  m_impl->command.bindIndexBuffer(m_impl->device.buffers.at(buffer).native, 0,
                                  vk::IndexType::eUint32,
                                  m_impl->device.dispatch);
}
void VulkanCommandList::Draw(PrimitiveTopology, uint32_t count, uint32_t first,
                             uint32_t instances, uint32_t firstInstance)
{
  m_impl->command.draw(count, instances, first, firstInstance,
                       m_impl->device.dispatch);
}
void VulkanCommandList::DrawIndexed(uint32_t count)
{
  DrawIndexedRange(count, 0, 0);
}
void VulkanCommandList::DrawIndexedRange(uint32_t count, uint32_t first,
                                         int32_t base)
{
  m_impl->command.drawIndexed(count, 1, first, base, 0,
                              m_impl->device.dispatch);
}
void VulkanCommandList::MultiDrawIndexedIndirect(BufferHandle buffer,
                                                 uint32_t count)
{
  m_impl->command.drawIndexedIndirect(
      m_impl->device.buffers.at(buffer).native, 0, count,
      sizeof(DrawIndexedIndirectCommand), m_impl->device.dispatch);
}
void VulkanCommandList::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
  m_impl->command.dispatch(x, y, z, m_impl->device.dispatch);
}
void VulkanCommandList::CopyBuffer(BufferHandle src, BufferHandle dst,
                                   uint64_t srcOffset, uint64_t dstOffset,
                                   uint64_t bytes)
{
  m_impl->command.copyBuffer(m_impl->device.buffers.at(src).native,
                             m_impl->device.buffers.at(dst).native,
                             vk::BufferCopy(srcOffset, dstOffset, bytes),
                             m_impl->device.dispatch);
}
void VulkanCommandList::BlitImage(ImageHandle src, ImageHandle dst,
                                  const BlitRegion& desc, ImageFilter filter)
{
  const auto layers = [](const ImageSubresourceLayers& value)
  {
    return vk::ImageSubresourceLayers(Aspect(value.aspectMask), value.mipLevel,
                                      value.baseArrayLayer, value.layerCount);
  };
  vk::ImageBlit2 region;
  region.setSrcSubresource(layers(desc.srcSubresource))
      .setDstSubresource(layers(desc.dstSubresource));
  for (unsigned i = 0; i < 2; ++i)
  {
    region.srcOffsets[i] = vk::Offset3D(
        desc.srcOffsets[i].x, desc.srcOffsets[i].y, desc.srcOffsets[i].z);
    region.dstOffsets[i] = vk::Offset3D(
        desc.dstOffsets[i].x, desc.dstOffsets[i].y, desc.dstOffsets[i].z);
  }
  vk::BlitImageInfo2 info;
  info.setSrcImage(m_impl->device.images.at(src).native)
      .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
      .setDstImage(m_impl->device.images.at(dst).native)
      .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
      .setRegions(region)
      .setFilter(filter == ImageFilter::Linear ? vk::Filter::eLinear
                                               : vk::Filter::eNearest);
  m_impl->command.blitImage2(info, m_impl->device.dispatch);
}
void VulkanCommandList::GenMipMaps(const GenMipMapsDesc& desc)
{
  const auto& image = m_impl->device.images.at(desc.image).desc;
  if (image.mipLevels < 2)
    return;
  ImageSubresourceRange range{ImageAspect::Color, 0, image.mipLevels, 0,
                              image.arrayLayers};
  ImageBarrier({.image = desc.image,
                .oldLayout = desc.oldLayout,
                .newLayout = ImageLayout::TransferDestination,
                .subresource = range});
  auto width = image.width, height = image.height, depth = image.depth;
  for (unsigned mip = 1; mip < image.mipLevels; ++mip)
  {
    range.baseMipLevel = mip - 1;
    range.mipLevelCount = 1;
    ImageBarrier({.image = desc.image,
                  .oldLayout = ImageLayout::TransferDestination,
                  .newLayout = ImageLayout::TransferSource,
                  .subresource = range});
    const auto w = std::max(1u, width / 2), h = std::max(1u, height / 2),
               z = std::max(1u, depth / 2);
    BlitImage(desc.image, desc.image,
              {{ImageAspect::Color, mip - 1, 0, image.arrayLayers},
               {{{0, 0, 0}, {width, height, depth}}},
               {ImageAspect::Color, mip, 0, image.arrayLayers},
               {{{0, 0, 0}, {w, h, z}}}},
              ImageFilter::Linear);
    ImageBarrier({.image = desc.image,
                  .oldLayout = ImageLayout::TransferSource,
                  .newLayout = desc.newLayout,
                  .subresource = range});
    width = w;
    height = h;
    depth = z;
  }
  range.baseMipLevel = image.mipLevels - 1;
  ImageBarrier({.image = desc.image,
                .oldLayout = ImageLayout::TransferDestination,
                .newLayout = desc.newLayout,
                .subresource = range});
}
void VulkanCommandList::BindTexture(uint32_t, ImageHandle)
{
  throw std::logic_error("Vulkan textures are bound through descriptor sets");
}
void VulkanCommandList::PushDebugGroup(std::string_view name,
                                       std::array<float, 4> color)
{
  auto& d = m_impl->device;
  if (d.dispatch.vkCmdBeginDebugUtilsLabelEXT)
  {
    const std::string text(name);
    m_impl->command.beginDebugUtilsLabelEXT(
        vk::DebugUtilsLabelEXT(text.c_str(), color), d.dispatch);
  }
}
void VulkanCommandList::PopDebugGroup()
{
  if (m_impl->device.dispatch.vkCmdEndDebugUtilsLabelEXT)
    m_impl->command.endDebugUtilsLabelEXT(m_impl->device.dispatch);
}
void VulkanCommandList::InsertDebugMarker(std::string_view name,
                                          std::array<float, 4> color)
{
  auto& d = m_impl->device;
  if (d.dispatch.vkCmdInsertDebugUtilsLabelEXT)
  {
    const std::string text(name);
    m_impl->command.insertDebugUtilsLabelEXT(
        vk::DebugUtilsLabelEXT(text.c_str(), color), d.dispatch);
  }
}
std::unique_ptr<IRenderCommandList>
VulkanRenderDevice::CreateCommandList(uint32_t queue)
{
  return std::make_unique<VulkanCommandList>(*this, queue);
}
void VulkanRenderDevice::Queue::Submit(const QueueSubmitInfo& info,
                                       FenceHandle fence)
{
  auto& d = *device.m_impl;
  std::vector<vk::CommandBufferSubmitInfo> commands;
  for (const auto* command : info.commandLists)
    commands.emplace_back(
        dynamic_cast<const VulkanCommandList&>(*command).Native());
  const auto convert = [&](auto values)
  {
    std::vector<vk::SemaphoreSubmitInfo> result;
    for (const auto& value : values)
      result.emplace_back(d.semaphores.at(value.semaphore), value.value,
                          vk::PipelineStageFlags2(
                              static_cast<VkPipelineStageFlags2>(value.stage)));
    return result;
  };
  const auto waits = convert(info.waits), signals = convert(info.signals);
  vk::SubmitInfo2 submit;
  submit.setWaitSemaphoreInfos(waits)
      .setCommandBufferInfos(commands)
      .setSignalSemaphoreInfos(signals);
  native.submit2(submit, fence ? d.fences.at(fence) : vk::Fence{}, d.dispatch);
}
} // namespace Lighthouse
