#include "VulkanDeviceState.hpp"
namespace Lighthouse
{
Flags<RenderQueueCapability>
VulkanRenderDevice::Queue::GetCapabilities() const noexcept
{
  return capabilities;
}
IRenderQueue* VulkanRenderDevice::GetQueue(std::uint32_t index)
{
  return index < m_queues.size() ? m_queues[index].get() : nullptr;
}
SurfaceHandle VulkanRenderDevice::CreateSurface(Window& window)
{
  auto& d = *m_impl;
  Impl::Surface surface{{}, static_cast<GLFWwindow*>(window.NativeHandle())};
  const auto result = static_cast<vk::Result>(glfwCreateWindowSurface(
      static_cast<vk::Instance::CType>(d.instance), surface.window, nullptr,
      reinterpret_cast<vk::SurfaceKHR::CType*>(&surface.native)));
  Require(result == vk::Result::eSuccess,
          "GLFW Vulkan surface creation failed");
  const auto h = d.Handle<SurfaceHandle>(d.nextSurface);
  try
  {
    d.surfaces.emplace(h, surface);
  }
  catch (...)
  {
    d.instance.destroySurfaceKHR(surface.native, nullptr, d.dispatch);
    throw;
  }
  return h;
}
void VulkanRenderDevice::DestroySurface(SurfaceHandle h)
{
  auto& d = *m_impl;
  for (const auto& slot : d.swaps)
    Require(!slot.value || slot.value->surfaceHandle != h,
            "Destroy swapchains before their surface");
  d.instance.destroySurfaceKHR(d.surfaces.at(h).native, nullptr, d.dispatch);
  d.surfaces.erase(h);
}

void VulkanRenderDevice::Queue::WaitIdle()
{
  native.waitIdle(device.m_impl->dispatch);
}

void VulkanRenderDevice::Impl::MakeSwapImages(SwapchainHandle handle)
{
  auto& swap = *Find(handle);
  const auto caps = physical.getSurfaceCapabilitiesKHR(swap.surface, dispatch);
  const auto formats = physical.getSurfaceFormatsKHR(swap.surface, dispatch);
  Require(!formats.empty(), "Surface has no formats");
  const auto selected = SelectSurfaceFormat(formats, swap.config.format);
  Require(selected.has_value(), "Requested swapchain format is unavailable");
  swap.format = *selected;
  EngineFormat(swap.format.format);
  int width{}, height{};
  glfwGetFramebufferSize(swap.window, &width, &height);
  swap.extent = caps.currentExtent;
  if (swap.extent.width == UINT32_MAX)
    swap.extent = vk::Extent2D(
        std::clamp(static_cast<unsigned>(std::max(width, 1)),
                   caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(static_cast<unsigned>(std::max(height, 1)),
                   caps.minImageExtent.height, caps.maxImageExtent.height));
  const auto modes = physical.getSurfacePresentModesKHR(swap.surface, dispatch);
  auto mode = vk::PresentModeKHR::eFifo;
  if (!swap.config.vsync)
  {
    if (std::find(modes.begin(), modes.end(), vk::PresentModeKHR::eMailbox) !=
        modes.end())
      mode = vk::PresentModeKHR::eMailbox;
    else if (std::find(modes.begin(), modes.end(),
                       vk::PresentModeKHR::eImmediate) != modes.end())
      mode = vk::PresentModeKHR::eImmediate;
  }
  auto count = std::max(caps.minImageCount, swap.config.minImageCount);
  if (caps.maxImageCount)
    count = std::min(count, caps.maxImageCount);
  auto usage = vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eTransferDst;
  Require((caps.supportedUsageFlags & usage) == usage,
          "Swapchain needs color and transfer-destination support");
  if (caps.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
    usage |= vk::ImageUsageFlagBits::eTransferSrc;
  vk::CompositeAlphaFlagBitsKHR alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  for (auto a : {vk::CompositeAlphaFlagBitsKHR::eOpaque,
                 vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                 vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                 vk::CompositeAlphaFlagBitsKHR::eInherit})
    if (caps.supportedCompositeAlpha & a)
    {
      alpha = a;
      break;
    }
  vk::SwapchainCreateInfoKHR create;
  create.setSurface(swap.surface)
      .setMinImageCount(count)
      .setImageFormat(swap.format.format)
      .setImageColorSpace(swap.format.colorSpace)
      .setImageExtent(swap.extent)
      .setImageArrayLayers(1)
      .setImageUsage(usage)
      .setImageSharingMode(vk::SharingMode::eExclusive)
      .setPreTransform(caps.currentTransform)
      .setCompositeAlpha(alpha)
      .setPresentMode(mode)
      .setClipped(true)
      .setOldSwapchain(swap.native);
  if (queueFamilies.size() > 1)
    create.setImageSharingMode(vk::SharingMode::eConcurrent)
        .setQueueFamilyIndices(queueFamilies);
  auto replacement = device.createSwapchainKHR(create, nullptr, dispatch);
  DropSwapImages(swap);
  if (swap.native)
    device.destroySwapchainKHR(swap.native, nullptr, dispatch);
  swap.native = replacement;
  for (auto native : device.getSwapchainImagesKHR(swap.native, dispatch))
  {
    auto image = Handle<ImageHandle>(nextImage);
    ImageDesc desc;
    desc.width = swap.extent.width;
    desc.height = swap.extent.height;
    desc.format = EngineFormat(swap.format.format);
    desc.usage = {ImageUsage::ColorAttachment, ImageUsage::TransferDestination};
    if (usage & vk::ImageUsageFlagBits::eTransferSrc)
      desc.usage |= ImageUsage::TransferSource;
    images.emplace(
        image, Image{native, {}, desc, vk::ImageLayout::eUndefined, handle});
    swap.images.push_back(image);
    vk::ImageViewCreateInfo info;
    info.setImage(native)
        .setViewType(vk::ImageViewType::e2D)
        .setFormat(swap.format.format)
        .setSubresourceRange(vk::ImageSubresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1));
    auto view = Handle<ImageViewHandle>(nextView);
    views.emplace(view,
                  View{device.createImageView(info, nullptr, dispatch),
                       ImageViewDesc{.image = image, .format = desc.format}});
    swap.views.push_back(view);
    swap.finished.push_back(device.createSemaphore({}, nullptr, dispatch));
    const auto semaphore = Handle<SemaphoreHandle>(nextSemaphore);
    semaphores.emplace(semaphore, swap.finished.back());
    swap.finishedHandles.push_back(semaphore);
  }
}
std::vector<SwapchainFormat>
VulkanRenderDevice::EnumerateAvailableSwapchainFormats(Window& window)
{
  auto& d = *m_impl;
  if (!d.swapchainEnabled)
    return {};
  vk::SurfaceKHR surface;
  const auto result = static_cast<vk::Result>(glfwCreateWindowSurface(
      static_cast<vk::Instance::CType>(d.instance),
      static_cast<GLFWwindow*>(window.NativeHandle()), nullptr,
      reinterpret_cast<vk::SurfaceKHR::CType*>(&surface)));
  Require(result == vk::Result::eSuccess,
          "Cannot create surface for swapchain format query");
  try
  {
    std::vector<SwapchainFormat> supported;
    const auto caps = d.physical.getSurfaceCapabilitiesKHR(surface, d.dispatch);
    const auto usage = vk::ImageUsageFlagBits::eColorAttachment |
                       vk::ImageUsageFlagBits::eTransferDst;
    if (std::any_of(d.queues.begin(), d.queues.end(),
                    [&](const auto& q)
                    {
                      return d.physical.getSurfaceSupportKHR(q.family, surface,
                                                             d.dispatch);
                    }) &&
        (caps.supportedUsageFlags & usage) == usage)
    {
      const auto formats = d.physical.getSurfaceFormatsKHR(surface, d.dispatch);
      for (auto format : {SwapchainFormat::SRGB, SwapchainFormat::HDR10,
                          SwapchainFormat::ScRGB})
        if (SelectSurfaceFormat(formats, format))
          supported.push_back(format);
    }
    d.instance.destroySurfaceKHR(surface, nullptr, d.dispatch);
    return supported;
  }
  catch (...)
  {
    d.instance.destroySurfaceKHR(surface, nullptr, d.dispatch);
    throw;
  }
}

std::optional<SwapchainInfo>
VulkanRenderDevice::GetSwapchainInfo(SwapchainHandle handle) const
{
  const auto* swap = m_impl->Find(handle);
  if (!swap)
    return std::nullopt;
  return SwapchainInfo{swap->config.format, EngineFormat(swap->format.format)};
}

SwapchainHandle
VulkanRenderDevice::CreateSwapchain(SurfaceHandle surface,
                                    const SwapchainConfig& config)
{
  auto& d = *m_impl;
  Require(d.swapchainEnabled && config.minImageCount > 0,
          "Vulkan swapchain unavailable or invalid image count");
  auto state = std::make_unique<Impl::Swap>();
  const auto& target = d.surfaces.at(surface);
  state->window = target.window;
  state->surface = target.native;
  state->surfaceHandle = surface;
  state->config = config;
  unsigned index = 0;
  while (index < d.swaps.size() && d.swaps[index].value)
    ++index;
  if (index == d.swaps.size())
    d.swaps.emplace_back();
  SwapchainHandle handle{index, d.swaps[index].generation, DeviceId()};
  d.swaps[index].value = std::move(state);
  try
  {
    auto& swap = *d.Find(handle);
    Require(std::any_of(d.queues.begin(), d.queues.end(),
                        [&](const auto& q)
                        {
                          return d.physical.getSurfaceSupportKHR(
                              q.family, swap.surface, d.dispatch);
                        }),
            "No created queue can present to this surface");
    swap.available = d.device.createSemaphore({}, nullptr, d.dispatch);
    swap.availableHandle = d.Handle<SemaphoreHandle>(d.nextSemaphore);
    d.semaphores.emplace(swap.availableHandle, swap.available);
    d.MakeSwapImages(handle);
  }
  catch (...)
  {
    DestroySwapchain(handle);
    throw;
  }
  return handle;
}
AcquiredSwapchainImage
VulkanRenderDevice::AcquireSwapchainImage(SwapchainHandle handle)
{
  auto& d = *m_impl;
  auto* swap = d.Find(handle);
  Require(swap && !swap->acquired, "Invalid or already acquired swapchain");
  // Reuse the acquire semaphore and transition command only after the previous
  // submission consumed acquisition and completed its presentation transition.
  int width{}, height{};
  glfwGetFramebufferSize(swap->window, &width, &height);
  Require(width > 0 && height > 0, "Cannot acquire minimized swapchain");
  if (swap->extent.width != static_cast<unsigned>(width) ||
      swap->extent.height != static_cast<unsigned>(height))
  {
    WaitIdle();
    d.MakeSwapImages(handle);
  }
  for (int tries = 0; tries < 2; ++tries)
    try
    {
      const auto image = d.device.acquireNextImageKHR(
          swap->native, UINT64_MAX, swap->available, {}, d.dispatch);
      swap->current = image.value;
      swap->acquired = true;
      const auto handle = swap->images[swap->current];
      return {handle, swap->views[swap->current], swap->availableHandle,
              swap->finishedHandles[swap->current],
              static_cast<ImageLayout>(d.images.at(handle).layout)};
    }
    catch (const vk::OutOfDateKHRError&)
    {
      WaitIdle();
      d.MakeSwapImages(handle);
    }
  throw std::runtime_error("Vulkan swapchain remained out of date");
}
bool VulkanRenderDevice::DestroySwapchain(SwapchainHandle handle)
{
  auto& d = *m_impl;
  auto* swap = d.Find(handle);
  if (!swap)
    return false;
  WaitIdle();
  d.DropSwapImages(*swap);
  if (swap->native)
    d.device.destroySwapchainKHR(swap->native, nullptr, d.dispatch);
  d.semaphores.erase(swap->availableHandle);
  if (swap->available)
    d.device.destroySemaphore(swap->available, nullptr, d.dispatch);

  auto& slot = d.swaps[handle.index];
  slot.value.reset();
  if (++slot.generation == 0)
    ++slot.generation;
  return true;
}
bool VulkanRenderDevice::IsValid(SwapchainHandle h) const noexcept
{
  return m_impl->Find(h) != nullptr;
}
void VulkanRenderDevice::Queue::Present(
    SwapchainHandle handle, std::span<const SemaphoreHandle> waitSemaphores)
{
  auto& d = *device.m_impl;
  auto* swap = d.Find(handle);
  Require(swap && swap->acquired, "Present requires an acquired image");
  std::vector<vk::Semaphore> waits;
  for (auto handle : waitSemaphores)
    waits.push_back(d.semaphores.at(handle));
  vk::PresentInfoKHR present;
  present.setWaitSemaphores(waits)
      .setSwapchains(swap->native)
      .setImageIndices(swap->current);
  bool outOfDate = false;
  try
  {
    const auto result = native.presentKHR(present, d.dispatch);
    outOfDate = result == vk::Result::eSuboptimalKHR;
  }
  catch (const vk::OutOfDateKHRError&)
  {
    outOfDate = true;
  }
  swap->acquired = false;
  if (outOfDate)
  {
    d.device.waitIdle(d.dispatch);
    d.MakeSwapImages(handle);
  }
}

} // namespace Lighthouse
