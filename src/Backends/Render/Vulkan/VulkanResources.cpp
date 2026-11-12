#include "VulkanDeviceState.hpp"
namespace Lighthouse
{
BufferHandle VulkanRenderDevice::CreateBuffer(const BufferDesc& desc)
{
  Require(desc.size > 0 && desc.initialData.size() <= desc.size,
          "Invalid buffer size/data");
  auto& d = *m_impl;
  vk::BufferUsageFlags usage = vk::BufferUsageFlagBits::eTransferSrc |
                               vk::BufferUsageFlagBits::eTransferDst;
  if (desc.usage.Contains(BufferUsage::Vertex))
    usage |= vk::BufferUsageFlagBits::eVertexBuffer;
  if (desc.usage.Contains(BufferUsage::Index))
    usage |= vk::BufferUsageFlagBits::eIndexBuffer;
  if (desc.usage.Contains(BufferUsage::Uniform))
    usage |= vk::BufferUsageFlagBits::eUniformBuffer;
  if (desc.usage.Contains(BufferUsage::Storage))
    usage |= vk::BufferUsageFlagBits::eStorageBuffer;
  if (desc.usage.Contains(BufferUsage::Indirect))
    usage |= vk::BufferUsageFlagBits::eIndirectBuffer;
  vk::BufferCreateInfo info({}, desc.size, usage, vk::SharingMode::eExclusive);
  if (d.queueFamilies.size() > 1)
    info.setSharingMode(vk::SharingMode::eConcurrent)
        .setQueueFamilyIndices(d.queueFamilies);
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO;
  const bool readable = (desc.memory.requiredAccess & MemoryAccess::CPURead) !=
                        MemoryAccess::None;
  const bool writable = (desc.memory.requiredAccess & MemoryAccess::CPUWrite) !=
                        MemoryAccess::None;
  if (readable || writable)
  {
    allocation.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    allocation.flags =
        (readable ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
                  : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  }
  else
  {
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  }
  vk::Buffer::CType native{};
  Impl::Buffer buffer;
  VmaAllocationInfo mapped{};
  AllocationCheck(vmaCreateBuffer(
      d.allocator,
      reinterpret_cast<const vk::BufferCreateInfo::NativeType*>(&info),
      &allocation, &native, &buffer.allocation, &mapped));
  buffer.native = vk::Buffer(native);
  buffer.desc = {desc.size, desc.usage, desc.memory};
  auto handle = d.Handle<BufferHandle>(d.nextBuffer);
  try
  {
    if (!desc.initialData.empty())
    {
      if (readable || writable)
      {
        void* initialMapping{};
        AllocationCheck(
            vmaMapMemory(d.allocator, buffer.allocation, &initialMapping));
        std::memcpy(initialMapping, desc.initialData.data(),
                    desc.initialData.size());
        const auto flushed = vmaFlushAllocation(d.allocator, buffer.allocation,
                                                0, desc.initialData.size());
        vmaUnmapMemory(d.allocator, buffer.allocation);
        AllocationCheck(flushed);
      }
      else
      {
        const auto staging = CreateBuffer(
            {.size = desc.initialData.size(),
             .usage = BufferUsage::TransferSrc,
             .initialData = desc.initialData,
             .memory = {.requiredAccess = MemoryAccess::CPUWrite}});
        try
        {
          d.Run(
              [&](vk::CommandBuffer cmd)
              {
                cmd.copyBuffer(d.buffers.at(staging).native, buffer.native,
                               vk::BufferCopy(0, 0, desc.initialData.size()),
                               d.dispatch);
              });
        }
        catch (...)
        {
          DestroyBuffer(staging);
          throw;
        }
        DestroyBuffer(staging);
      }
    }
    d.buffers.emplace(handle, buffer);
  }
  catch (...)
  {
    vmaDestroyBuffer(d.allocator, native, buffer.allocation);
    throw;
  }
  return handle;
}
std::span<std::byte> VulkanRenderDevice::MapMemory(BufferHandle h)
{
  auto& b = m_impl->buffers.at(h);
  void* mapped{};
  AllocationCheck(vmaMapMemory(m_impl->allocator, b.allocation, &mapped));
  return {static_cast<std::byte*>(mapped), size_t(b.desc.size)};
}
void VulkanRenderDevice::UnmapMemory(BufferHandle h)
{
  vmaUnmapMemory(m_impl->allocator, m_impl->buffers.at(h).allocation);
}
void VulkanRenderDevice::FlushMappedMemoryRange(BufferHandle h,
                                                std::uint64_t offset,
                                                std::uint64_t size)
{
  AllocationCheck(vmaFlushAllocation(
      m_impl->allocator, m_impl->buffers.at(h).allocation, offset, size));
}
void VulkanRenderDevice::InvalidateMappedMemoryRange(BufferHandle h,
                                                     std::uint64_t offset,
                                                     std::uint64_t size)
{
  AllocationCheck(vmaInvalidateAllocation(
      m_impl->allocator, m_impl->buffers.at(h).allocation, offset, size));
}
FenceHandle VulkanRenderDevice::CreateFence(bool signaled)
{
  auto& d = *m_impl;
  auto h = d.Handle<FenceHandle>(d.nextFence);
  d.fences.emplace(
      h, d.device.createFence(
             vk::FenceCreateInfo(signaled ? vk::FenceCreateFlagBits::eSignaled
                                          : vk::FenceCreateFlags{}),
             nullptr, d.dispatch));
  return h;
}
void VulkanRenderDevice::DestroyFence(FenceHandle h)
{
  m_impl->device.destroyFence(m_impl->fences.at(h), nullptr, m_impl->dispatch);
  m_impl->fences.erase(h);
}
void VulkanRenderDevice::ResetFence(FenceHandle h)
{
  m_impl->device.resetFences(m_impl->fences.at(h), m_impl->dispatch);
}
bool VulkanRenderDevice::GetFenceStatus(FenceHandle h)
{
  return m_impl->device.getFenceStatus(
             m_impl->fences.at(h), m_impl->dispatch) == vk::Result::eSuccess;
}
bool VulkanRenderDevice::WaitForFence(FenceHandle h, std::uint64_t timeout)
{
  return m_impl->device.waitForFences(m_impl->fences.at(h), true, timeout,
                                      m_impl->dispatch) == vk::Result::eSuccess;
}
SemaphoreHandle VulkanRenderDevice::CreateSemaphore(SemaphoreType type,
                                                    std::uint64_t initialValue)
{
  auto& d = *m_impl;
  auto h = d.Handle<SemaphoreHandle>(d.nextSemaphore);
  vk::SemaphoreTypeCreateInfo kind(type == SemaphoreType::Timeline
                                       ? vk::SemaphoreType::eTimeline
                                       : vk::SemaphoreType::eBinary,
                                   initialValue);
  vk::SemaphoreCreateInfo info;
  info.pNext = &kind;
  d.semaphores.emplace(h, d.device.createSemaphore(info, nullptr, d.dispatch));
  return h;
}
void VulkanRenderDevice::DestroySemaphore(SemaphoreHandle h)
{
  m_impl->device.destroySemaphore(m_impl->semaphores.at(h), nullptr,
                                  m_impl->dispatch);
  m_impl->semaphores.erase(h);
}
std::uint64_t VulkanRenderDevice::GetSemaphoreCounterValue(SemaphoreHandle h)
{
  return m_impl->device.getSemaphoreCounterValue(m_impl->semaphores.at(h),
                                                 m_impl->dispatch);
}
void VulkanRenderDevice::SignalSemaphore(SemaphoreHandle h, std::uint64_t value)
{
  m_impl->device.signalSemaphore(
      vk::SemaphoreSignalInfo(m_impl->semaphores.at(h), value),
      m_impl->dispatch);
}
bool VulkanRenderDevice::WaitSemaphore(SemaphoreHandle h, std::uint64_t value,
                                       std::uint64_t timeout)
{
  auto semaphore = m_impl->semaphores.at(h);
  return m_impl->device.waitSemaphores(
             vk::SemaphoreWaitInfo({}, 1, &semaphore, &value), timeout,
             m_impl->dispatch) == vk::Result::eSuccess;
}
std::optional<BufferMetadata>
VulkanRenderDevice::GetBufferDesc(BufferHandle h) const noexcept
{
  auto it = m_impl->buffers.find(h);
  return it == m_impl->buffers.end() ? std::nullopt
                                     : std::optional(it->second.desc);
}
bool VulkanRenderDevice::IsValid(BufferHandle h) const noexcept
{
  return m_impl->buffers.contains(h);
}
void VulkanRenderDevice::DestroyBuffer(BufferHandle h)
{
  auto& d = *m_impl;
  auto it = d.buffers.find(h);
  if (it == d.buffers.end())
    return;
  vmaDestroyBuffer(d.allocator,
                   static_cast<vk::Buffer::CType>(it->second.native),
                   it->second.allocation);
  d.buffers.erase(it);
}

ImageHandle VulkanRenderDevice::CreateImage(const ImageDesc& desc)
{
  auto& d = *m_impl;
  Require(desc.width && desc.height && desc.depth && desc.mipLevels &&
              desc.arrayLayers,
          "Image dimensions must be nonzero");
  vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferSrc |
                              vk::ImageUsageFlagBits::eTransferDst;
  if (desc.usage.Contains(ImageUsage::Sampled))
    usage |= vk::ImageUsageFlagBits::eSampled;
  if (desc.usage.Contains(ImageUsage::Storage))
    usage |= vk::ImageUsageFlagBits::eStorage;
  if (desc.usage.Contains(ImageUsage::ColorAttachment))
    usage |= vk::ImageUsageFlagBits::eColorAttachment;
  if (desc.usage.Contains(ImageUsage::DepthAttachment) ||
      desc.usage.Contains(ImageUsage::DepthStencilAttachment))
    usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
  auto type = desc.type == ImageType::Image1D   ? vk::ImageType::e1D
              : desc.type == ImageType::Image3D ? vk::ImageType::e3D
                                                : vk::ImageType::e2D;
  auto flags =
      (desc.type == ImageType::Cube || desc.type == ImageType::CubeArray)
          ? vk::ImageCreateFlagBits::eCubeCompatible
          : vk::ImageCreateFlags{};
  vk::ImageCreateInfo info;
  info.setFlags(flags)
      .setImageType(type)
      .setFormat(Format(desc.format))
      .setExtent(vk::Extent3D(desc.width, desc.height, desc.depth))
      .setMipLevels(desc.mipLevels)
      .setArrayLayers(desc.arrayLayers)
      .setSamples(Samples(desc.sampleCount))
      .setTiling(vk::ImageTiling::eOptimal)
      .setUsage(usage)
      .setSharingMode(vk::SharingMode::eExclusive);
  if (d.queueFamilies.size() > 1)
    info.setSharingMode(vk::SharingMode::eConcurrent)
        .setQueueFamilyIndices(d.queueFamilies);
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  vk::Image::CType native{};
  Impl::Image image;
  image.desc = desc;
  image.desc.initialData = {};
  image.desc.initialMips.clear();
  AllocationCheck(vmaCreateImage(
      d.allocator,
      reinterpret_cast<const vk::ImageCreateInfo::NativeType*>(&info),
      &allocation, &native, &image.allocation, nullptr));
  image.native = vk::Image(native);
  auto handle = d.Handle<ImageHandle>(d.nextImage);
  try
  {
    d.images.emplace(handle, image);
  }
  catch (...)
  {
    vmaDestroyImage(d.allocator, native, image.allocation);
    throw;
  }
  if (!desc.initialData.empty() || !desc.initialMips.empty())
  {
    BufferHandle staging{};
    try
    {
      Require(desc.initialData.empty() || desc.initialMips.empty(),
              "Specify initialData or initialMips, not both");
      auto levels = desc.initialMips;
      if (!desc.initialData.empty())
        levels = {desc.initialData};
      Require((desc.type == ImageType::Image2D ||
               desc.type == ImageType::Image3D) &&
                  desc.arrayLayers == 1 && desc.sampleCount == 1 &&
                  levels.size() <= desc.mipLevels,
              "Initial image upload requires single-layer 2D/3D mip data");
      Require(desc.format != ImageFormat::Depth16 &&
                  desc.format != ImageFormat::Depth32 &&
                  desc.format != ImageFormat::Depth24Stencil8,
              "Initial depth image upload is unsupported");
      const auto pixelSize = ImageData::BytesPerPixel(desc.format);
      const auto alignment = std::lcm<std::size_t>(4, pixelSize);
      std::vector<std::byte> packed;
      std::vector<vk::BufferImageCopy> regions;
      auto width = desc.width, height = desc.height, depth = desc.depth;
      for (std::size_t level = 0; level < levels.size(); ++level)
      {
        const auto bytes = levels[level];
        Require(bytes.size() == std::size_t(width) * height * depth * pixelSize,
                "Initial mip byte size does not match dimensions");
        const auto offset =
            (packed.size() + alignment - 1) / alignment * alignment;
        packed.resize(offset);
        packed.insert(packed.end(), bytes.begin(), bytes.end());
        vk::BufferImageCopy region;
        region.setBufferOffset(offset)
            .setImageSubresource(vk::ImageSubresourceLayers(
                vk::ImageAspectFlagBits::eColor, level, 0, 1))
            .setImageExtent(vk::Extent3D(width, height, depth));
        regions.push_back(region);
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
        depth = std::max(1u, depth / 2);
      }
      staging = CreateBuffer({.size = packed.size(),
                              .usage = BufferUsage::TransferSrc,
                              .initialData = packed});
      d.Run(
          [&](vk::CommandBuffer cmd)
          {
            d.Barrier(cmd, handle, vk::ImageLayout::eUndefined,
                      vk::ImageLayout::eTransferDstOptimal);
            cmd.copyBufferToImage(d.buffers.at(staging).native, image.native,
                                  vk::ImageLayout::eTransferDstOptimal, regions,
                                  d.dispatch);
          });
      d.images.at(handle).layout = vk::ImageLayout::eTransferDstOptimal;
      DestroyBuffer(staging);
    }
    catch (...)
    {
      if (staging)
        DestroyBuffer(staging);
      DestroyImage(handle);
      throw;
    }
  }
  return handle;
}
ImageLayout VulkanRenderDevice::GetImageLayout(ImageHandle image) const
{
  return static_cast<ImageLayout>(m_impl->images.at(image).layout);
}
ImageViewHandle VulkanRenderDevice::CreateImageView(const ImageViewDesc& desc)
{
  auto& d = *m_impl;
  auto& image = d.images.at(desc.image);
  const auto format =
      desc.format == ImageFormat::Undefined ? image.desc.format : desc.format;
  Require(format == image.desc.format,
          "Image view format reinterpretation is not enabled");
  vk::ImageViewType type;
  switch (desc.type)
  {
  case ImageType::Image1D:
    type = vk::ImageViewType::e1D;
    break;
  case ImageType::Image2D:
    type = vk::ImageViewType::e2D;
    break;
  case ImageType::Image3D:
    type = vk::ImageViewType::e3D;
    break;
  case ImageType::Image2DArray:
    type = vk::ImageViewType::e2DArray;
    break;
  case ImageType::Cube:
    type = vk::ImageViewType::eCube;
    break;
  case ImageType::CubeArray:
    type = vk::ImageViewType::eCubeArray;
    break;
  }
  Require(desc.mipLevelCount && desc.arrayLayerCount &&
              desc.baseMipLevel + desc.mipLevelCount <= image.desc.mipLevels &&
              desc.baseArrayLayer + desc.arrayLayerCount <=
                  image.desc.arrayLayers,
          "Image view exceeds image");
  vk::ImageViewCreateInfo info;
  info.setImage(image.native)
      .setViewType(type)
      .setFormat(Format(format))
      .setSubresourceRange(vk::ImageSubresourceRange(
          Aspect(format), desc.baseMipLevel, desc.mipLevelCount,
          desc.baseArrayLayer, desc.arrayLayerCount));
  auto h = d.Handle<ImageViewHandle>(d.nextView);
  d.views.emplace(
      h, Impl::View{d.device.createImageView(info, nullptr, d.dispatch), desc});
  return h;
}
void VulkanRenderDevice::DestroyImageView(ImageViewHandle h)
{
  auto& d = *m_impl;
  auto it = d.views.find(h);
  if (it == d.views.end())
    return;
  Require(!d.images.at(it->second.desc.image).swap,
          "Swapchain owns this image view");
  d.device.destroyImageView(it->second.native, nullptr, d.dispatch);
  d.views.erase(it);
}
void VulkanRenderDevice::DestroyImage(ImageHandle h)
{
  auto& d = *m_impl;
  auto it = d.images.find(h);
  if (it == d.images.end())
    return;
  Require(!it->second.swap, "Swapchain owns this image");
  for (auto v = d.views.begin(); v != d.views.end();)
    if (v->second.desc.image == h)
    {
      d.device.destroyImageView(v->second.native, nullptr, d.dispatch);
      v = d.views.erase(v);
    }
    else
      ++v;
  vmaDestroyImage(d.allocator, static_cast<vk::Image::CType>(it->second.native),
                  it->second.allocation);
  d.images.erase(it);
}
std::vector<std::byte> VulkanRenderDevice::ReadImage(ImageHandle h)
{
  auto& d = *m_impl;
  auto& image = d.images.at(h);
  if (image.swap)
  {
    auto* swap = d.Find(image.swap);
    Require(swap && swap->acquired &&
                image.desc.usage.Contains(ImageUsage::TransferSource),
            "Readback requires a submitted, acquired transfer-source swapchain "
            "image");
  }
  const auto f = image.desc.format;
  Require(image.desc.type == ImageType::Image2D &&
              image.desc.sampleCount == 1 &&
              (f == ImageFormat::RGBA8 || f == ImageFormat::RGBA8Srgb ||
               f == ImageFormat::BGRA8 || f == ImageFormat::BGRA8Srgb),
          "ReadImage requires a single-sampled RGBA/BGRA8 2D image");
  const auto size =
      static_cast<std::size_t>(image.desc.width) * image.desc.height * 4;
  auto staging =
      CreateBuffer({.size = size,
                    .usage = BufferUsage::TransferDst,
                    .memory = {.requiredAccess = MemoryAccess::CPURead |
                                                 MemoryAccess::GPUWrite}});
  std::vector<std::byte> result(size);
  try
  {
    auto old = image.layout;
    d.Run(
        [&](vk::CommandBuffer cmd)
        {
          d.Barrier(cmd, h, old, vk::ImageLayout::eTransferSrcOptimal);
          vk::BufferImageCopy region;
          region
              .setImageSubresource(vk::ImageSubresourceLayers(
                  vk::ImageAspectFlagBits::eColor, 0, 0, 1))
              .setImageExtent(
                  vk::Extent3D(image.desc.width, image.desc.height, 1));
          cmd.copyImageToBuffer(
              image.native, vk::ImageLayout::eTransferSrcOptimal,
              d.buffers.at(staging).native, region, d.dispatch);
          if (old != vk::ImageLayout::eUndefined)
            d.Barrier(cmd, h, vk::ImageLayout::eTransferSrcOptimal, old);
        });
    if (old == vk::ImageLayout::eUndefined)
      image.layout = vk::ImageLayout::eTransferSrcOptimal;
    auto& buffer = d.buffers.at(staging);
    AllocationCheck(
        vmaInvalidateAllocation(d.allocator, buffer.allocation, 0, size));
    auto mapped = MapMemory(staging);
    std::memcpy(result.data(), mapped.data(), size);
    UnmapMemory(staging);
    DestroyBuffer(staging);
  }
  catch (...)
  {
    DestroyBuffer(staging);
    throw;
  }
  if (f == ImageFormat::BGRA8 || f == ImageFormat::BGRA8Srgb)
    for (std::size_t i = 0; i < size; i += 4)
      std::swap(result[i], result[i + 2]);
  return result;
}

} // namespace Lighthouse
