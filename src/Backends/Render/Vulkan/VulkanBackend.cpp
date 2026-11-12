#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanRenderDevice.hpp>
#include <algorithm>
#include <cstring>
#include <vulkan/vulkan.hpp>

namespace Lighthouse
{
bool VulkanBackend::Initialize(std::string& diagnostic)
{
  m_capabilities = {};
  try
  {
    vk::detail::DispatchLoaderDynamic dispatch{vkGetInstanceProcAddr};
    if (!dispatch.vkEnumerateInstanceVersion ||
        vk::enumerateInstanceVersion(dispatch) < vk::ApiVersion14)
      throw std::runtime_error("Vulkan 1.4 loader is required");
    vk::ApplicationInfo app;
    app.setPApplicationName("Lighthouse preflight")
        .setApiVersion(vk::ApiVersion14);
    vk::InstanceCreateInfo info;
    info.setPApplicationInfo(&app);
    auto instance = vk::createInstanceUnique(info, nullptr, dispatch);
    dispatch.init(*instance);
    for (auto physical : instance->enumeratePhysicalDevices(dispatch))
    {
      if (physical.getProperties(dispatch).apiVersion < vk::ApiVersion14)
        continue;
      vk::PhysicalDeviceFeatures2 features;
      vk::PhysicalDeviceVulkan11Features f11;
      vk::PhysicalDeviceVulkan12Features f12;
      vk::PhysicalDeviceVulkan13Features f13;
      features.pNext = &f11;
      f11.pNext = &f12;
      f12.pNext = &f13;
      physical.getFeatures2(&features, dispatch);
      if (!f12.timelineSemaphore || !f13.dynamicRendering ||
          !f13.synchronization2)
        continue;
      m_capabilities.computeShaders = true;
      m_capabilities.bindlessResources =
          f12.runtimeDescriptorArray && f12.descriptorBindingPartiallyBound &&
          f12.shaderSampledImageArrayNonUniformIndexing;
      m_capabilities.multiview = f11.multiview;
      diagnostic.clear();
      return true;
    }
    throw std::runtime_error("No Vulkan 1.4 device with dynamic rendering and "
                             "required descriptor features");
  }
  catch (const std::exception& error)
  {
    diagnostic = error.what();
    return false;
  }
}
GraphicsCapabilities VulkanBackend::Capabilities() const
{
  return m_capabilities;
}
std::unique_ptr<IRenderDevice>
VulkanBackend::CreateDevice(const RenderDeviceCreateInfo& config)
{
  auto device = std::make_unique<VulkanRenderDevice>(config, m_surfaces);
  const auto caps = device->GetCapabilities();
  m_capabilities = {.computeShaders = caps.computeShaders,
                    .bindlessResources = caps.bindlessResources,
                    .rayTracing = caps.rayTracing,
                    .meshShaders = caps.meshShaders,
                    .asyncCompute = caps.asyncCompute,
                    .multiview = caps.multiView};
  return device;
}
void VulkanBackend::Shutdown() noexcept
{
  m_surfaces.clear();
  m_capabilities = {};
}
} // namespace Lighthouse
