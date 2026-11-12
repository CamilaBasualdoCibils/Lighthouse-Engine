#define VMA_IMPLEMENTATION
#include "VulkanDeviceState.hpp"
namespace Lighthouse
{
void VulkanRenderDevice::Impl::Init(
    const RenderDeviceCreateInfo& config, std::uint64_t ownerId,
    std::span<const std::pair<SurfaceHandle, Window*>> targets)
{
  owner = ownerId;
  // GLFW must use the same loader that owns this Hpp instance (vcpkg and
  // system libvulkan may coexist on the host).
  glfwInitVulkanLoader(vkGetInstanceProcAddr);
  Require(dispatch.vkEnumerateInstanceVersion &&
              vk::enumerateInstanceVersion(dispatch) >= vk::ApiVersion14,
          "Vulkan 1.4 loader is required");
  const auto available =
      vk::enumerateInstanceExtensionProperties(nullptr, dispatch);
  std::vector<const char*> extensions;
  for (const char* name : {"VK_KHR_surface", "VK_KHR_xlib_surface",
                           "VK_KHR_xcb_surface", "VK_KHR_wayland_surface",
                           "VK_EXT_debug_utils", "VK_EXT_swapchain_colorspace"})
    if (std::any_of(available.begin(), available.end(), [&](const auto& p)
                    { return std::strcmp(p.extensionName, name) == 0; }))
      extensions.push_back(name);
  vk::ApplicationInfo app;
  const std::string applicationName(config.applicationName);
  app.setPApplicationName(applicationName.c_str())
      .setPEngineName("Lighthouse")
      .setApiVersion(vk::ApiVersion14);
  std::vector<const char*> layers;
  if (config.enableValidation)
  {
    const auto found = vk::enumerateInstanceLayerProperties(dispatch);
    Require(std::any_of(found.begin(), found.end(),
                        [](const auto& l)
                        {
                          return std::strcmp(l.layerName,
                                             "VK_LAYER_KHRONOS_validation") ==
                                 0;
                        }),
            "Requested Vulkan validation layer is unavailable");
    layers.push_back("VK_LAYER_KHRONOS_validation");
  }
  vk::InstanceCreateInfo instanceInfo;
  instanceInfo.setPApplicationInfo(&app)
      .setPEnabledExtensionNames(extensions)
      .setPEnabledLayerNames(layers);
  instance = vk::createInstance(instanceInfo, nullptr, dispatch);
  dispatch.init(instance);
  for (auto [handle, window] : targets)
  {
    Surface surface{{}, static_cast<GLFWwindow*>(window->NativeHandle())};
    const auto result = static_cast<vk::Result>(glfwCreateWindowSurface(
        static_cast<vk::Instance::CType>(instance), surface.window, nullptr,
        reinterpret_cast<vk::SurfaceKHR::CType*>(&surface.native)));
    Require(result == vk::Result::eSuccess,
            "Cannot create requested presentation surface");
    try
    {
      surfaces.emplace(handle, surface);
    }
    catch (...)
    {
      instance.destroySurfaceKHR(surface.native, nullptr, dispatch);
      throw;
    }
  }
  for (const auto& request : config.queues)
  {
    Require(request.minCount <= request.preferredCount,
            "Queue minimum exceeds preferred count");
    if (request.preferredCount &&
        (static_cast<std::uint32_t>(request.capabilities) &
         static_cast<std::uint32_t>(RenderQueueCapability::Present)) != 0)
      Require(surfaces.contains(request.presentSurface),
              "Presentation queue request needs a backend-created surface");
  }
  std::string failures;
  auto candidates = instance.enumeratePhysicalDevices(dispatch);
  std::stable_sort(candidates.begin(), candidates.end(),
                   [&](auto a, auto b)
                   {
                     return a.getProperties(dispatch).deviceType ==
                                vk::PhysicalDeviceType::eDiscreteGpu &&
                            b.getProperties(dispatch).deviceType !=
                                vk::PhysicalDeviceType::eDiscreteGpu;
                   });
  for (auto candidate : candidates)
  {
    auto props = candidate.getProperties(dispatch);
    if (props.apiVersion < vk::ApiVersion14)
    {
      failures +=
          std::string(props.deviceName.data()) + ": older than Vulkan 1.4; ";
      continue;
    }
    const auto exts =
        candidate.enumerateDeviceExtensionProperties(nullptr, dispatch);
    const auto has = [&](const char* name)
    {
      return std::any_of(exts.begin(), exts.end(), [&](const auto& e)
                         { return std::strcmp(e.extensionName, name) == 0; });
    };
    vk::PhysicalDeviceFeatures2 features;
    vk::PhysicalDeviceVulkan11Features f11;
    vk::PhysicalDeviceVulkan12Features f12;
    vk::PhysicalDeviceVulkan13Features f13;
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracing;
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR acceleration;
    vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaders;
    features.pNext = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    if (has("VK_KHR_ray_tracing_pipeline") &&
        has("VK_KHR_acceleration_structure"))
    {
      f13.pNext = &rayTracing;
      rayTracing.pNext = &acceleration;
    }
    if (has("VK_EXT_mesh_shader"))
    {
      if (f13.pNext)
        acceleration.pNext = &meshShaders;
      else
        f13.pNext = &meshShaders;
    }
    candidate.getFeatures2(&features, dispatch);
    if (!f12.timelineSemaphore || !f13.dynamicRendering ||
        !f13.synchronization2)
    {
      failures += "Required rendering/descriptor features missing; ";
      continue;
    }
    auto families = candidate.getQueueFamilyProperties(dispatch);
    // Match native queue slots to requests. Augmenting assignments allow a
    // constrained request to displace a flexible one without losing its count.
    struct Slot
    {
      std::uint32_t family;
      int request = -1;
    };
    std::vector<Slot> slots;
    for (std::uint32_t f = 0; f < families.size(); ++f)
      for (std::uint32_t i = 0; i < families[f].queueCount; ++i)
        slots.push_back({f});
    const auto supports =
        [&](std::uint32_t f, const RenderQueueRequest& request)
    {
      const auto bits = static_cast<std::uint32_t>(request.capabilities);
      const Flags<RenderQueueCapability> required{request.capabilities};
      const auto nativeFlags = families[f].queueFlags;
      if (required.Contains(RenderQueueCapability::Graphics) &&
          !(nativeFlags & vk::QueueFlagBits::eGraphics))
        return false;
      if (required.Contains(RenderQueueCapability::Compute) &&
          !(nativeFlags & vk::QueueFlagBits::eCompute))
        return false;
      if (required.Contains(RenderQueueCapability::Transfer) &&
          !(nativeFlags &
            (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eGraphics |
             vk::QueueFlagBits::eCompute)))
        return false;
      if (required.Contains(RenderQueueCapability::Present) &&
          (!has("VK_KHR_swapchain") ||
           !candidate.getSurfaceSupportKHR(
               f, surfaces.at(request.presentSurface).native, dispatch)))
        return false;
      return (bits & ~15u) == 0;
    };
    std::vector<std::vector<size_t>> candidatesByRequest(config.queues.size());
    for (size_t request = 0; request < config.queues.size(); ++request)
    {
      if (!config.queues[request].preferredCount)
        continue;
      for (size_t slot = 0; slot < slots.size(); ++slot)
        if (supports(slots[slot].family, config.queues[request]))
          candidatesByRequest[request].push_back(slot);
      // Prefer dedicated families when the request needs fewer capabilities.
      const auto cost = [&](size_t slot)
      {
        const auto flags = families[slots[slot].family].queueFlags;
        const Flags<RenderQueueCapability> requested{
            config.queues[request].capabilities};
        unsigned extras = 0;
        if ((flags & vk::QueueFlagBits::eGraphics) &&
            !requested.Contains(RenderQueueCapability::Graphics))
          ++extras;
        if ((flags & vk::QueueFlagBits::eCompute) &&
            !requested.Contains(RenderQueueCapability::Compute))
          ++extras;
        return extras;
      };
      std::stable_sort(candidatesByRequest[request].begin(),
                       candidatesByRequest[request].end(),
                       [&](size_t a, size_t b) { return cost(a) < cost(b); });
    }
    std::function<bool(std::uint32_t, std::vector<bool>&)> assign;
    assign = [&](std::uint32_t request, std::vector<bool>& visited)
    {
      for (size_t slot : candidatesByRequest[request])
      {
        if (visited[slot])
          continue;
        visited[slot] = true;
        if (slots[slot].request < 0 || assign(slots[slot].request, visited))
        {
          slots[slot].request = static_cast<int>(request);
          return true;
        }
      }
      return false;
    };
    bool requiredAvailable = true;
    for (std::uint32_t request = 0;
         request < config.queues.size() && requiredAvailable; ++request)
    {
      if (config.queues[request].minCount > slots.size())
      {
        requiredAvailable = false;
        break;
      }
      for (std::uint32_t i = 0; i < config.queues[request].minCount; ++i)
      {
        std::vector<bool> visited(slots.size());
        if (!assign(request, visited))
        {
          requiredAvailable = false;
          break;
        }
      }
    }
    if (!requiredAvailable)
    {
      failures += "Queue minimum counts unavailable; ";
      continue;
    }
    for (std::uint32_t request = 0; request < config.queues.size(); ++request)
    {
      auto count = static_cast<std::uint32_t>(
          std::count_if(slots.begin(), slots.end(), [request](const Slot& slot)
                        { return slot.request == static_cast<int>(request); }));
      while (count < config.queues[request].preferredCount)
      {
        std::vector<bool> visited(slots.size());
        if (!assign(request, visited))
          break;
        ++count;
      }
    }
    if (std::none_of(slots.begin(), slots.end(),
                     [](const Slot& slot) { return slot.request >= 0; }))
    {
      failures += "No queues requested or available; ";
      continue;
    }
    physical = candidate;
    properties = props;
    swapchainEnabled = has("VK_KHR_swapchain");
    std::vector<const char*> enabled;
    if (swapchainEnabled)
      enabled.push_back("VK_KHR_swapchain");
    vk::PhysicalDeviceVulkan11Features enabled11;
    enabled11.multiview = f11.multiview;
    vk::PhysicalDeviceVulkan12Features enabled12;
    enabled12.timelineSemaphore = true;
    enabled12.descriptorBindingUniformBufferUpdateAfterBind =
        f12.descriptorBindingUniformBufferUpdateAfterBind;
    enabled12.descriptorBindingStorageBufferUpdateAfterBind =
        f12.descriptorBindingStorageBufferUpdateAfterBind;
    enabled12.descriptorBindingSampledImageUpdateAfterBind =
        f12.descriptorBindingSampledImageUpdateAfterBind;
    enabled12.runtimeDescriptorArray = f12.runtimeDescriptorArray;
    enabled12.descriptorBindingPartiallyBound =
        f12.descriptorBindingPartiallyBound;
    enabled12.shaderSampledImageArrayNonUniformIndexing =
        f12.shaderSampledImageArrayNonUniformIndexing;
    vk::PhysicalDeviceVulkan13Features enabled13;
    enabled13.dynamicRendering = true;
    enabled13.synchronization2 = true;
    enabled11.pNext = &enabled12;
    enabled12.pNext = &enabled13;
    vk::PhysicalDeviceFeatures enabledBase;
    enabledBase.multiDrawIndirect = features.features.multiDrawIndirect;
    enabledBase.samplerAnisotropy = features.features.samplerAnisotropy;
    std::vector<std::vector<float>> priorities(families.size());
    for (const auto& slot : slots)
      if (slot.request >= 0)
        priorities[slot.family].push_back(1.f);
    std::vector<vk::DeviceQueueCreateInfo> queueInfos;
    for (std::uint32_t f = 0; f < families.size(); ++f)
      if (!priorities[f].empty())
        queueInfos.emplace_back(vk::DeviceQueueCreateFlags{}, f, priorities[f]);
    vk::DeviceCreateInfo create;
    create.setQueueCreateInfos(queueInfos)
        .setPEnabledExtensionNames(enabled)
        .setPEnabledFeatures(&enabledBase)
        .setPNext(&enabled11);
    device = physical.createDevice(create, nullptr, dispatch);
    dispatch.init(device);
    std::vector<std::uint32_t> used(families.size());
    for (std::uint32_t request = 0; request < config.queues.size(); ++request)
      for (const auto& slot : slots)
      {
        if (slot.request != static_cast<int>(request))
          continue;
        Flags<RenderQueueCapability> caps;
        const auto flags = families[slot.family].queueFlags;
        if (flags & vk::QueueFlagBits::eGraphics)
          caps |= RenderQueueCapability::Graphics;
        if (flags & vk::QueueFlagBits::eCompute)
          caps |= RenderQueueCapability::Compute;
        if (flags &
            (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eGraphics |
             vk::QueueFlagBits::eCompute))
          caps |= RenderQueueCapability::Transfer;
        if (swapchainEnabled)
          caps |= RenderQueueCapability::Present;
        queues.push_back(
            {device.getQueue(slot.family, used[slot.family]++, dispatch),
             slot.family, caps});
      }
    auto primary = std::find_if(
        queues.begin(), queues.end(), [](const QueueEntry& q)
        { return q.capabilities.Contains(RenderQueueCapability::Graphics); });
    if (primary == queues.end())
      primary = std::find_if(
          queues.begin(), queues.end(), [](const QueueEntry& q)
          { return q.capabilities.Contains(RenderQueueCapability::Transfer); });
    Require(primary != queues.end(), "Lighthouse requires a command-capable "
                                     "queue for resource initialization");
    queue = primary->native;
    family = primary->family;
    for (const auto& q : queues)
      if (std::find(queueFamilies.begin(), queueFamilies.end(), q.family) ==
          queueFamilies.end())
        queueFamilies.push_back(q.family);
    updateAfterBind = {
        bool(enabled12.descriptorBindingUniformBufferUpdateAfterBind),
        bool(enabled12.descriptorBindingStorageBufferUpdateAfterBind),
        bool(enabled12.descriptorBindingSampledImageUpdateAfterBind)};
    capabilities.deviceName = properties.deviceName.data();
    capabilities.apiVersion = props.apiVersion;
    capabilities.headless = true;
    capabilities.computeShaders = true;
    capabilities.dynamicRendering = true;
    capabilities.shaderObjects = false;
    capabilities.supportedMultiview = f11.multiview;
    capabilities.multiView = enabled11.multiview;
    capabilities.supportedBindlessResources =
        f12.runtimeDescriptorArray &&
        f12.shaderSampledImageArrayNonUniformIndexing;
    capabilities.bindlessResources =
        enabled12.runtimeDescriptorArray &&
        enabled12.descriptorBindingPartiallyBound &&
        enabled12.shaderSampledImageArrayNonUniformIndexing;
    capabilities.hardwareRayTracing =
        rayTracing.rayTracingPipeline && acceleration.accelerationStructure &&
        props.deviceType != vk::PhysicalDeviceType::eCpu;
    capabilities.supportedMeshShaders = meshShaders.meshShader;
#ifdef LIGHTHOUSE_IMGUI
    capabilities.imgui = swapchainEnabled;
#endif
    break;
  }
  Require(static_cast<bool>(device),
          ("No suitable Vulkan 1.4 device: " + failures).c_str());
  VmaVulkanFunctions functions{};
  functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  functions.vkGetDeviceProcAddr = dispatch.vkGetDeviceProcAddr;
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.instance = static_cast<vk::Instance::CType>(instance);
  allocatorInfo.physicalDevice =
      static_cast<vk::PhysicalDevice::CType>(physical);
  allocatorInfo.device = static_cast<vk::Device::CType>(device);
  allocatorInfo.vulkanApiVersion = vk::ApiVersion14;
  allocatorInfo.pVulkanFunctions = &functions;
  AllocationCheck(vmaCreateAllocator(&allocatorInfo, &allocator));
  immediatePool = device.createCommandPool(
      vk::CommandPoolCreateInfo(
          vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family),
      nullptr, dispatch);
  fence = device.createFence({}, nullptr, dispatch);
  const std::array poolSizes{
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 4096),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 4096),
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 4096)};
  descriptorPool = device.createDescriptorPool(
      vk::DescriptorPoolCreateInfo(
          vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet |
              vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind,
          4096, poolSizes),
      nullptr, dispatch);
}

VulkanRenderDevice::Impl::~Impl()
{
  if (device)
  {
    try
    {
      device.waitIdle(dispatch);
    }
    catch (...)
    {
    }
    for (auto& [_, pool] : commandPools)
      device.destroyCommandPool(pool, nullptr, dispatch);
    for (auto& [_, p] : pipelines)
      device.destroyPipeline(p.native, nullptr, dispatch);
    for (auto& [_, module] : modules)
      device.destroyShaderModule(module.native, nullptr, dispatch);
    for (auto& [_, v] : views)
      device.destroyImageView(v.native, nullptr, dispatch);
    for (auto& [_, s] : samplers)
      device.destroySampler(s, nullptr, dispatch);
    for (auto& [_, l] : layouts)
      device.destroyPipelineLayout(l.native, nullptr, dispatch);
    if (descriptorPool)
      device.destroyDescriptorPool(descriptorPool, nullptr, dispatch);
    for (auto& [_, l] : setLayouts)
      device.destroyDescriptorSetLayout(l.native, nullptr, dispatch);
    for (auto& [_, b] : buffers)
      if (b.allocation)
        vmaDestroyBuffer(allocator, static_cast<vk::Buffer::CType>(b.native),
                         b.allocation);
    for (auto& [_, i] : images)
      if (i.allocation)
        vmaDestroyImage(allocator, static_cast<vk::Image::CType>(i.native),
                        i.allocation);
    for (auto& slot : swaps)
      if (slot.value)
      {
        auto& s = *slot.value;
        if (s.native)
          device.destroySwapchainKHR(s.native, nullptr, dispatch);
        semaphores.erase(s.availableHandle);
        for (auto handle : s.finishedHandles)
          semaphores.erase(handle);
        if (s.available)
          device.destroySemaphore(s.available, nullptr, dispatch);
        for (auto semaphore : s.finished)
          device.destroySemaphore(semaphore, nullptr, dispatch);
      }
    if (immediatePool)
      device.destroyCommandPool(immediatePool, nullptr, dispatch);
    for (const auto& [_, marker] : fences)
      device.destroyFence(marker, nullptr, dispatch);
    for (const auto& [_, semaphore] : semaphores)
      device.destroySemaphore(semaphore, nullptr, dispatch);
    if (fence)
      device.destroyFence(fence, nullptr, dispatch);
    if (allocator)
      vmaDestroyAllocator(allocator);
    device.destroy(nullptr, dispatch);
  }
  for (auto [_, surface] : surfaces)
    instance.destroySurfaceKHR(surface.native, nullptr, dispatch);
  if (instance)
    instance.destroy(nullptr, dispatch);
}

VulkanRenderDevice::VulkanRenderDevice(
    const RenderDeviceCreateInfo& config,
    std::span<const std::pair<SurfaceHandle, Window*>> surfaces)
    : m_impl(std::make_unique<Impl>())
{
  m_impl->Init(config, DeviceId(), surfaces);
  for (const auto& q : m_impl->queues)
    m_queues.push_back(
        std::make_unique<Queue>(*this, q.native, q.family, q.capabilities));
}
VulkanRenderDevice::~VulkanRenderDevice() = default;
RenderCapabilities VulkanRenderDevice::GetCapabilities() const
{
  return m_impl->capabilities;
}
void VulkanRenderDevice::WaitIdle()
{
  if (m_impl->device)
    m_impl->device.waitIdle(m_impl->dispatch);
}

} // namespace Lighthouse
