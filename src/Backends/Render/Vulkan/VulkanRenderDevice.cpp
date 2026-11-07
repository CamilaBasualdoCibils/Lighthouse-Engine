#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanCommandList.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanRenderDevice.hpp>
#include <vulkan/vulkan.hpp>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/Detail/ImGuiGlfwRouting.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#endif
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Lighthouse
{
namespace
{
template <class T> struct Hash
{
  std::size_t operator()(T h) const noexcept
  {
    return h.index;
  }
};
template <class H, class T> using Map = std::unordered_map<H, T, Hash<H>>;
void Require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
// VMA is a C library. Native C handles/results are confined to these allocation
// calls (and the GLFW/ImGui adapters); all Vulkan rendering uses Vulkan-Hpp.
template <class Result> void AllocationCheck(Result rawResult)
{
  const auto result = static_cast<vk::Result>(rawResult);
  if (result != vk::Result::eSuccess)
    throw std::runtime_error("VMA allocation/mapping failed: " +
                             vk::to_string(result));
}
vk::Format Format(ImageFormat f)
{
  switch (f)
  {
#define F(a, b)                                                                \
  case ImageFormat::a:                                                         \
    return vk::Format::b
    F(R8, eR8Unorm);
    F(RG8, eR8G8Unorm);
    F(RGB8, eR8G8B8Unorm);
    F(RGBA8, eR8G8B8A8Unorm);
    F(RGB10A2, eA2B10G10R10UnormPack32);
    F(BGR10A2, eA2R10G10B10UnormPack32);
    F(BGRA8, eB8G8R8A8Unorm);
    F(BGRA8Srgb, eB8G8R8A8Srgb);
    F(RGBA8Srgb, eR8G8B8A8Srgb);
    F(R16f, eR16Sfloat);
    F(RG16f, eR16G16Sfloat);
    F(RGB16f, eR16G16B16Sfloat);
    F(RGBA16f, eR16G16B16A16Sfloat);
    F(R32f, eR32Sfloat);
    F(RG32f, eR32G32Sfloat);
    F(RGB32f, eR32G32B32Sfloat);
    F(RGBA32f, eR32G32B32A32Sfloat);
    F(Depth16, eD16Unorm);
    F(Depth24Stencil8, eD24UnormS8Uint);
    F(Depth32, eD32Sfloat);
#undef F
  default:
    throw std::invalid_argument("Undefined Vulkan image format");
  }
}
ImageFormat EngineFormat(vk::Format f)
{
  if (f == vk::Format::eR8G8B8A8Unorm)
    return ImageFormat::RGBA8;
  if (f == vk::Format::eR8G8B8A8Srgb)
    return ImageFormat::RGBA8Srgb;
  if (f == vk::Format::eB8G8R8A8Unorm)
    return ImageFormat::BGRA8;
  if (f == vk::Format::eB8G8R8A8Srgb)
    return ImageFormat::BGRA8Srgb;
  if (f == vk::Format::eR16G16B16A16Sfloat)
    return ImageFormat::RGBA16f;
  if (f == vk::Format::eA2R10G10B10UnormPack32)
    return ImageFormat::BGR10A2;
  if (f == vk::Format::eA2B10G10R10UnormPack32)
    return ImageFormat::RGB10A2;
  throw std::runtime_error("Unsupported presentation format");
}
std::optional<vk::SurfaceFormatKHR>
SelectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available,
                    SwapchainFormat requested)
{
  vk::ColorSpaceKHR colorSpace;
  std::vector<vk::Format> candidates;
  switch (requested)
  {
  case SwapchainFormat::SRGB:
    colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    // Preserve the existing display-encoded UNORM presentation contract.
    candidates = {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm,
                  vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
    break;
  case SwapchainFormat::HDR10:
    colorSpace = vk::ColorSpaceKHR::eHdr10St2084EXT;
    candidates = {vk::Format::eA2B10G10R10UnormPack32,
                  vk::Format::eA2R10G10B10UnormPack32};
    break;
  case SwapchainFormat::ScRGB:
    colorSpace = vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;
    candidates = {vk::Format::eR16G16B16A16Sfloat};
    break;
  default:
    return std::nullopt;
  }
  for (auto format : candidates)
    for (const auto& supported : available)
      if (supported.colorSpace == colorSpace &&
          (supported.format == format ||
           supported.format == vk::Format::eUndefined))
        return vk::SurfaceFormatKHR(format, colorSpace);
  return std::nullopt;
}

vk::ImageAspectFlags Aspect(ImageFormat f)
{
  if (f == ImageFormat::Depth24Stencil8)
    return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
  if (f == ImageFormat::Depth16 || f == ImageFormat::Depth32)
    return vk::ImageAspectFlagBits::eDepth;
  return vk::ImageAspectFlagBits::eColor;
}
vk::ImageAspectFlags Aspect(Flags<ImageAspect> a)
{
  vk::ImageAspectFlags result;
  if (a.Contains(ImageAspect::Color))
    result |= vk::ImageAspectFlagBits::eColor;
  if (a.Contains(ImageAspect::Depth))
    result |= vk::ImageAspectFlagBits::eDepth;
  if (a.Contains(ImageAspect::Stencil))
    result |= vk::ImageAspectFlagBits::eStencil;
  return result;
}
vk::ShaderStageFlags Stages(ShaderStageFlags s)
{
  vk::ShaderStageFlags flags;
  if (static_cast<unsigned>(s) & 1)
    flags |= vk::ShaderStageFlagBits::eVertex;
  if (static_cast<unsigned>(s) & 2)
    flags |= vk::ShaderStageFlagBits::eFragment;
  if (static_cast<unsigned>(s) & 4)
    flags |= vk::ShaderStageFlagBits::eCompute;
  return flags;
}
vk::ShaderStageFlagBits Stage(ShaderStageFlags s)
{
  if (s == ShaderStageFlags::Vertex)
    return vk::ShaderStageFlagBits::eVertex;
  if (s == ShaderStageFlags::Fragment)
    return vk::ShaderStageFlagBits::eFragment;
  if (s == ShaderStageFlags::Compute)
    return vk::ShaderStageFlagBits::eCompute;
  throw std::invalid_argument("Shader module must have one stage");
}
vk::DescriptorType Descriptor(DescriptorType t)
{
  switch (t)
  {
  case DescriptorType::UniformBuffer:
    return vk::DescriptorType::eUniformBuffer;
  case DescriptorType::StorageBuffer:
    return vk::DescriptorType::eStorageBuffer;
  case DescriptorType::CombinedImageSampler:
    return vk::DescriptorType::eCombinedImageSampler;
  }
  throw std::invalid_argument("Unknown descriptor type");
}
vk::BlendFactor BlendFactorNative(BlendFactor value)
{
  switch (value)
  {
  case BlendFactor::Zero:
    return vk::BlendFactor::eZero;
  case BlendFactor::One:
    return vk::BlendFactor::eOne;
  case BlendFactor::SourceColor:
    return vk::BlendFactor::eSrcColor;
  case BlendFactor::OneMinusSourceColor:
    return vk::BlendFactor::eOneMinusSrcColor;
  case BlendFactor::DestinationColor:
    return vk::BlendFactor::eDstColor;
  case BlendFactor::OneMinusDestinationColor:
    return vk::BlendFactor::eOneMinusDstColor;
  case BlendFactor::SourceAlpha:
    return vk::BlendFactor::eSrcAlpha;
  case BlendFactor::OneMinusSourceAlpha:
    return vk::BlendFactor::eOneMinusSrcAlpha;
  case BlendFactor::DestinationAlpha:
    return vk::BlendFactor::eDstAlpha;
  case BlendFactor::OneMinusDestinationAlpha:
    return vk::BlendFactor::eOneMinusDstAlpha;
  case BlendFactor::ConstantColor:
    return vk::BlendFactor::eConstantColor;
  case BlendFactor::OneMinusConstantColor:
    return vk::BlendFactor::eOneMinusConstantColor;
  case BlendFactor::ConstantAlpha:
    return vk::BlendFactor::eConstantAlpha;
  case BlendFactor::OneMinusConstantAlpha:
    return vk::BlendFactor::eOneMinusConstantAlpha;
  case BlendFactor::SourceAlphaSaturate:
    return vk::BlendFactor::eSrcAlphaSaturate;
  }
  throw std::invalid_argument("Unsupported BlendFactor");
}

vk::BlendOp BlendOperationNative(BlendOperation value)
{
  switch (value)
  {
  case BlendOperation::Add:
    return vk::BlendOp::eAdd;
  case BlendOperation::Subtract:
    return vk::BlendOp::eSubtract;
  case BlendOperation::ReverseSubtract:
    return vk::BlendOp::eReverseSubtract;
  case BlendOperation::Min:
    return vk::BlendOp::eMin;
  case BlendOperation::Max:
    return vk::BlendOp::eMax;
  }
  throw std::invalid_argument("Unsupported BlendOperation");
}

vk::PrimitiveTopology Topology(PrimitiveTopology t)
{
  switch (t)
  {
  case PrimitiveTopology::TriangleList:
    return vk::PrimitiveTopology::eTriangleList;
  case PrimitiveTopology::TriangleStrip:
    return vk::PrimitiveTopology::eTriangleStrip;
  case PrimitiveTopology::PointList:
    return vk::PrimitiveTopology::ePointList;
  case PrimitiveTopology::LineList:
    return vk::PrimitiveTopology::eLineList;
  case PrimitiveTopology::LineStrip:
    return vk::PrimitiveTopology::eLineStrip;
  }
  throw std::invalid_argument("Unsupported topology");
}
vk::Format VertexFormatNative(VertexFormat f)
{
  switch (f)
  {
#define F(a, b)                                                                \
  case VertexFormat::a:                                                        \
    return vk::Format::b
    F(Float, eR32Sfloat);
    F(Float2, eR32G32Sfloat);
    F(Float3, eR32G32B32Sfloat);
    F(Float4, eR32G32B32A32Sfloat);
    F(Int, eR32Sint);
    F(Int2, eR32G32Sint);
    F(Int3, eR32G32B32Sint);
    F(Int4, eR32G32B32A32Sint);
    F(UInt, eR32Uint);
    F(UInt2, eR32G32Uint);
    F(UInt3, eR32G32B32Uint);
    F(UInt4, eR32G32B32A32Uint);
    F(UNorm8x4, eR8G8B8A8Unorm);
    F(SNorm8x4, eR8G8B8A8Snorm);
#undef F
  }
  throw std::invalid_argument("Unsupported vertex format");
}
vk::SampleCountFlagBits Samples(unsigned n)
{
  switch (n)
  {
  case 1:
    return vk::SampleCountFlagBits::e1;
  case 2:
    return vk::SampleCountFlagBits::e2;
  case 4:
    return vk::SampleCountFlagBits::e4;
  case 8:
    return vk::SampleCountFlagBits::e8;
  default:
    throw std::invalid_argument("Unsupported sample count");
  }
}
} // namespace

struct VulkanRenderDevice::Impl
{
  vk::detail::DispatchLoaderDynamic dispatch{vkGetInstanceProcAddr};
  vk::Instance instance;
  vk::PhysicalDevice physical;
  vk::PhysicalDeviceProperties properties;
  vk::Device device;
  vk::Queue queue;
  std::uint32_t family{};
  VmaAllocator allocator{};
  vk::CommandPool immediatePool;
  vk::Fence fence;
  vk::DescriptorPool descriptorPool;
  RenderCapabilities capabilities;
  std::uint64_t owner{};
  std::uint32_t nextImage{1}, nextView{1}, nextBuffer{1}, nextModule{1},
      nextSetLayout{1}, nextSet{1}, nextLayout{1}, nextSampler{1},
      nextPipeline{1};
  bool swapchainEnabled{};
  struct Buffer
  {
    vk::Buffer native;
    VmaAllocation allocation{};
    void* mapped{};
    BufferMetadata desc;
  };
  struct Image
  {
    vk::Image native;
    VmaAllocation allocation{};
    ImageDesc desc;
    vk::ImageLayout layout{vk::ImageLayout::eUndefined};
    SwapchainHandle swap;
  };
  struct View
  {
    vk::ImageView native;
    ImageViewDesc desc;
  };
  struct Module
  {
    std::vector<std::uint32_t> words;
    ShaderStageFlags stage;
    std::string entry;
  };
  struct SetLayout
  {
    vk::DescriptorSetLayout native;
    std::map<unsigned, DescriptorBindingDesc> bindings;
  };
  struct Set
  {
    vk::DescriptorSet native;
    DescriptorSetLayoutHandle layout;
    std::map<std::pair<unsigned, unsigned>, DescriptorResource> resources;
  };
  struct Layout
  {
    vk::PipelineLayout native;
    std::vector<DescriptorSetLayoutHandle> sets;
  };
  struct Pipeline
  {
    std::array<vk::ShaderEXT, 3> shaders{};
    GraphicsPipelineDesc desc;
    bool compute{};
  };
  struct Swap
  {
    GLFWwindow* window{};
    vk::SurfaceKHR surface;
    vk::SwapchainKHR native;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
    std::vector<ImageHandle> images;
    std::vector<ImageViewHandle> views;
    vk::Semaphore available;
    std::vector<vk::Semaphore> finished;
    std::uint32_t current{}, generation{1};
    bool acquired{}, waitPending{}, ready{};
    SwapchainConfig config;
  };
  struct SwapSlot
  {
    unsigned generation{1};
    std::unique_ptr<Swap> value;
  };
  std::vector<SwapSlot> swaps;
  Map<BufferHandle, Buffer> buffers;
  Map<ImageHandle, Image> images;
  Map<ImageViewHandle, View> views;
  Map<ShaderModuleHandle, Module> modules;
  Map<DescriptorSetLayoutHandle, SetLayout> setLayouts;
  Map<DescriptorSetHandle, Set> sets;
  Map<PipelineLayoutHandle, Layout> layouts;
  Map<SamplerHandle, vk::Sampler> samplers;
  Map<GraphicsPipelineHandle, Pipeline> pipelines;
#ifdef LIGHTHOUSE_IMGUI
  struct Gui
  {
    GLFWwindow* host{};
  };
  std::unordered_map<ImGuiContext*, Gui> gui;
#endif
  template <class H> H Handle(unsigned& counter)
  {
    return {counter++, 1, owner};
  }
  Swap* Find(SwapchainHandle h)
  {
    if (!h || h.owner != owner || h.index >= swaps.size())
      return nullptr;
    auto& slot = swaps[h.index];
    return slot.generation == h.generation ? slot.value.get() : nullptr;
  }
  void Init(const DeviceConfig& config, std::uint64_t ownerId);
  ~Impl();
  void Run(const std::function<void(vk::CommandBuffer)>& record,
           const std::vector<vk::Semaphore>& waits = {},
           const std::vector<vk::Semaphore>& signals = {})
  {
    auto cmd = device
                   .allocateCommandBuffers(
                       vk::CommandBufferAllocateInfo(
                           immediatePool, vk::CommandBufferLevel::ePrimary, 1),
                       dispatch)
                   .front();
    try
    {
      cmd.begin(vk::CommandBufferBeginInfo(
                    vk::CommandBufferUsageFlagBits::eOneTimeSubmit),
                dispatch);
      record(cmd);
      cmd.end(dispatch);
      Execute(cmd, waits, signals);
    }
    catch (...)
    {
      device.freeCommandBuffers(immediatePool, cmd, dispatch);
      throw;
    }
    device.freeCommandBuffers(immediatePool, cmd, dispatch);
  }
  void Execute(vk::CommandBuffer cmd,
               const std::vector<vk::Semaphore>& waits = {},
               const std::vector<vk::Semaphore>& signals = {})
  {
    std::vector<vk::SemaphoreSubmitInfo> waitInfos, signalInfos;
    for (auto semaphore : waits)
      waitInfos.emplace_back(semaphore, 0,
                             vk::PipelineStageFlagBits2::eAllCommands);
    for (auto semaphore : signals)
      signalInfos.emplace_back(semaphore, 0,
                               vk::PipelineStageFlagBits2::eAllCommands);
    vk::CommandBufferSubmitInfo commandInfo(cmd);
    vk::SubmitInfo2 submit;
    submit.setWaitSemaphoreInfos(waitInfos)
        .setCommandBufferInfos(commandInfo)
        .setSignalSemaphoreInfos(signalInfos);
    device.resetFences(fence, dispatch);
    queue.submit2(submit, fence, dispatch);
    Require(device.waitForFences(fence, true, UINT64_MAX, dispatch) ==
                vk::Result::eSuccess,
            "Vulkan submission did not complete");
  }
  void Barrier(vk::CommandBuffer cmd, ImageHandle handle, vk::ImageLayout old,
               vk::ImageLayout next)
  {
    auto& image = images.at(handle);
    vk::ImageMemoryBarrier2 barrier;
    barrier
        .setSrcStageMask(old == vk::ImageLayout::eUndefined
                             ? vk::PipelineStageFlags2{}
                             : vk::PipelineStageFlagBits2::eAllCommands)
        .setSrcAccessMask(old == vk::ImageLayout::eUndefined
                              ? vk::AccessFlags2{}
                              : vk::AccessFlagBits2::eMemoryRead |
                                    vk::AccessFlagBits2::eMemoryWrite)
        .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
        .setDstAccessMask(vk::AccessFlagBits2::eMemoryRead |
                          vk::AccessFlagBits2::eMemoryWrite)
        .setOldLayout(old)
        .setNewLayout(next)
        .setSrcQueueFamilyIndex(vk::QueueFamilyIgnored)
        .setDstQueueFamilyIndex(vk::QueueFamilyIgnored)
        .setImage(image.native)
        .setSubresourceRange(vk::ImageSubresourceRange(
            Aspect(image.desc.format), 0, image.desc.mipLevels, 0,
            image.desc.arrayLayers));
    vk::DependencyInfo dependency;
    dependency.setImageMemoryBarriers(barrier);
    cmd.pipelineBarrier2(dependency, dispatch);
  }
  void MakeSwapImages(SwapchainHandle handle);
  void DropSwapImages(Swap& swap)
  {
    for (auto h : swap.views)
    {
      auto it = views.find(h);
      if (it != views.end())
      {
        device.destroyImageView(it->second.native, nullptr, dispatch);
        views.erase(it);
      }
    }
    for (auto h : swap.images)
      images.erase(h);
    for (auto semaphore : swap.finished)
      device.destroySemaphore(semaphore, nullptr, dispatch);
    swap.finished.clear();
    swap.views.clear();
    swap.images.clear();
  }
};

void VulkanRenderDevice::Impl::Init(const DeviceConfig& config,
                                    std::uint64_t ownerId)
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
    if (!has("VK_EXT_shader_object"))
    {
      failures += "VK_EXT_shader_object missing; ";
      continue;
    }
    vk::PhysicalDeviceFeatures2 features;
    vk::PhysicalDeviceVulkan11Features f11;
    vk::PhysicalDeviceVulkan12Features f12;
    vk::PhysicalDeviceVulkan13Features f13;
    vk::PhysicalDeviceShaderObjectFeaturesEXT shaderObject;
    vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracing;
    vk::PhysicalDeviceAccelerationStructureFeaturesKHR acceleration;
    vk::PhysicalDeviceMeshShaderFeaturesEXT meshShaders;
    features.pNext = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    f13.pNext = &shaderObject;
    if (has("VK_KHR_ray_tracing_pipeline") &&
        has("VK_KHR_acceleration_structure"))
    {
      shaderObject.pNext = &rayTracing;
      rayTracing.pNext = &acceleration;
    }
    if (has("VK_EXT_mesh_shader"))
    {
      if (shaderObject.pNext)
        acceleration.pNext = &meshShaders;
      else
        shaderObject.pNext = &meshShaders;
    }
    candidate.getFeatures2(&features, dispatch);
    if (!f13.dynamicRendering || !f13.synchronization2 ||
        !shaderObject.shaderObject ||
        !f12.descriptorBindingUniformBufferUpdateAfterBind ||
        !f12.descriptorBindingStorageBufferUpdateAfterBind ||
        !f12.descriptorBindingSampledImageUpdateAfterBind)
    {
      failures += "Required rendering/descriptor features missing; ";
      continue;
    }
    auto families = candidate.getQueueFamilyProperties(dispatch);
    auto q =
        std::find_if(families.begin(), families.end(),
                     [](const auto& p)
                     {
                       return p.queueCount &&
                              (p.queueFlags & vk::QueueFlagBits::eGraphics) &&
                              (p.queueFlags & vk::QueueFlagBits::eCompute);
                     });
    if (q == families.end())
      continue;
    physical = candidate;
    properties = props;
    family = static_cast<unsigned>(q - families.begin());
    swapchainEnabled = has("VK_KHR_swapchain");
    std::vector<const char*> enabled{"VK_EXT_shader_object"};
    if (swapchainEnabled)
      enabled.push_back("VK_KHR_swapchain");
    if (has("VK_KHR_dynamic_rendering"))
      enabled.push_back("VK_KHR_dynamic_rendering");
    vk::PhysicalDeviceVulkan11Features enabled11;
    // VUID-vkCmdDraw-None-10772 forbids multiview with shader objects.
    enabled11.multiview = false;
    vk::PhysicalDeviceVulkan12Features enabled12;
    enabled12.descriptorBindingUniformBufferUpdateAfterBind = true;
    enabled12.descriptorBindingStorageBufferUpdateAfterBind = true;
    enabled12.descriptorBindingSampledImageUpdateAfterBind = true;
    enabled12.runtimeDescriptorArray = f12.runtimeDescriptorArray;
    enabled12.descriptorBindingPartiallyBound =
        f12.descriptorBindingPartiallyBound;
    enabled12.shaderSampledImageArrayNonUniformIndexing =
        f12.shaderSampledImageArrayNonUniformIndexing;
    vk::PhysicalDeviceVulkan13Features enabled13;
    enabled13.dynamicRendering = true;
    enabled13.synchronization2 = true;
    vk::PhysicalDeviceShaderObjectFeaturesEXT enabledShader;
    enabledShader.shaderObject = true;
    enabled11.pNext = &enabled12;
    enabled12.pNext = &enabled13;
    enabled13.pNext = &enabledShader;
    vk::PhysicalDeviceFeatures enabledBase;
    enabledBase.multiDrawIndirect = features.features.multiDrawIndirect;
    enabledBase.samplerAnisotropy = features.features.samplerAnisotropy;
    const float priority = 1;
    vk::DeviceQueueCreateInfo queueInfo({}, family, 1, &priority);
    vk::DeviceCreateInfo create;
    create.setQueueCreateInfos(queueInfo)
        .setPEnabledExtensionNames(enabled)
        .setPEnabledFeatures(&enabledBase)
        .setPNext(&enabled11);
    device = physical.createDevice(create, nullptr, dispatch);
    dispatch.init(device);
    queue = device.getQueue(family, 0, dispatch);
    capabilities.deviceName = properties.deviceName.data();
    capabilities.apiVersion = props.apiVersion;
    capabilities.headless = true;
    capabilities.computeShaders = true;
    capabilities.dynamicRendering = true;
    capabilities.shaderObjects = true;
    capabilities.supportedMultiview = f11.multiview;
    capabilities.multiView = false;
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
          ("No suitable Vulkan 1.4 shader-object device: " + failures).c_str());
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
    for (auto& [_, p] : pipelines)
      for (auto shader : p.shaders)
        if (shader)
          device.destroyShaderEXT(shader, nullptr, dispatch);
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
        if (s.available)
          device.destroySemaphore(s.available, nullptr, dispatch);
        for (auto semaphore : s.finished)
          device.destroySemaphore(semaphore, nullptr, dispatch);
        if (s.surface)
          instance.destroySurfaceKHR(s.surface, nullptr, dispatch);
      }
    if (immediatePool)
      device.destroyCommandPool(immediatePool, nullptr, dispatch);
    if (fence)
      device.destroyFence(fence, nullptr, dispatch);
    if (allocator)
      vmaDestroyAllocator(allocator);
    device.destroy(nullptr, dispatch);
  }
  if (instance)
    instance.destroy(nullptr, dispatch);
}

VulkanRenderDevice::VulkanRenderDevice(const DeviceConfig& config)
    : m_impl(std::make_unique<Impl>())
{
  m_impl->Init(config, DeviceId());
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
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO;
  allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
  vk::Buffer::CType native{};
  Impl::Buffer buffer;
  VmaAllocationInfo mapped{};
  AllocationCheck(vmaCreateBuffer(
      d.allocator,
      reinterpret_cast<const vk::BufferCreateInfo::NativeType*>(&info),
      &allocation, &native, &buffer.allocation, &mapped));
  buffer.native = vk::Buffer(native);
  buffer.mapped = mapped.pMappedData;
  buffer.desc = {desc.size, desc.usage, desc.memory};
  auto handle = d.Handle<BufferHandle>(d.nextBuffer);
  try
  {
    if (!desc.initialData.empty())
    {
      std::memcpy(buffer.mapped, desc.initialData.data(),
                  desc.initialData.size());
      AllocationCheck(vmaFlushAllocation(d.allocator, buffer.allocation, 0,
                                         desc.initialData.size()));
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
void VulkanRenderDevice::UpdateBuffer(BufferHandle h, std::uint64_t offset,
                                      std::span<const std::byte> bytes)
{
  auto& d = *m_impl;
  auto& b = d.buffers.at(h);
  Require(offset <= b.desc.size && bytes.size() <= b.desc.size - offset,
          "Buffer update exceeds allocation");
  if (bytes.empty())
    return;
  WaitIdle();
  std::memcpy(static_cast<std::byte*>(b.mapped) + offset, bytes.data(),
              bytes.size());
  AllocationCheck(
      vmaFlushAllocation(d.allocator, b.allocation, offset, bytes.size()));
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
  WaitIdle();
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
  VmaAllocationCreateInfo allocation{};
  allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  vk::Image::CType native{};
  Impl::Image image;
  image.desc = desc;
  image.desc.initialData = {};
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
  if (!desc.initialData.empty())
  {
    BufferHandle staging{};
    try
    {
      Require(
          desc.type == ImageType::Image2D && desc.sampleCount == 1 &&
              (desc.format == ImageFormat::RGBA8 ||
               desc.format == ImageFormat::RGBA8Srgb) &&
              desc.initialData.size() == static_cast<std::size_t>(desc.width) *
                                             desc.height * desc.arrayLayers * 4,
          "Initial image data currently requires tightly packed RGBA8 2D data");
      staging = CreateBuffer({.size = desc.initialData.size(),
                              .usage = BufferUsage::TransferSrc,
                              .initialData = desc.initialData});
      d.Run(
          [&](vk::CommandBuffer cmd)
          {
            d.Barrier(cmd, handle, vk::ImageLayout::eUndefined,
                      vk::ImageLayout::eTransferDstOptimal);
            vk::BufferImageCopy region;
            region
                .setImageSubresource(vk::ImageSubresourceLayers(
                    vk::ImageAspectFlagBits::eColor, 0, 0, desc.arrayLayers))
                .setImageExtent(vk::Extent3D(desc.width, desc.height, 1));
            cmd.copyBufferToImage(d.buffers.at(staging).native, image.native,
                                  vk::ImageLayout::eTransferDstOptimal, region,
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
  WaitIdle();
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
  WaitIdle();
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
    Require(swap && swap->acquired && !swap->waitPending &&
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
      CreateBuffer({.size = size, .usage = BufferUsage::TransferDst});
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
    std::memcpy(result.data(), buffer.mapped, size);
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

ShaderModuleHandle
VulkanRenderDevice::CreateShaderModule(const ShaderModuleDesc& desc)
{
  Require(!desc.bytecode.empty() && desc.bytecode.size() % 4 == 0 &&
              !desc.entryPoint.empty(),
          "Invalid SPIR-V shader");
  auto& d = *m_impl;
  Impl::Module module;
  module.words.resize(desc.bytecode.size() / 4);
  std::memcpy(module.words.data(), desc.bytecode.data(), desc.bytecode.size());
  module.stage = desc.stage;
  module.entry = desc.entryPoint;
  Stage(desc.stage);
  auto h = d.Handle<ShaderModuleHandle>(d.nextModule);
  d.modules.emplace(h, std::move(module));
  return h;
}
void VulkanRenderDevice::DestroyModule(ShaderModuleHandle h)
{
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
    flags.push_back(vk::DescriptorBindingFlagBits::eUpdateAfterBind);
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
  d.sets.emplace(h, Impl::Set{native, desc.layout, {}});
  return h;
}
PipelineLayoutHandle
VulkanRenderDevice::CreatePipelineLayout(const PipelineLayoutDesc& desc)
{
  auto& d = *m_impl;
  std::vector<vk::DescriptorSetLayout> sets;
  for (auto h : desc.sets)
    sets.push_back(d.setLayouts.at(h).native);
  auto native = d.device.createPipelineLayout(
      vk::PipelineLayoutCreateInfo({}, sets), nullptr, d.dispatch);
  auto h = d.Handle<PipelineLayoutHandle>(d.nextLayout);
  d.layouts.emplace(h, Impl::Layout{native, desc.sets});
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
  WaitIdle();
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
      if (image.layout != vk::ImageLayout::eShaderReadOnlyOptimal)
      {
        auto old = image.layout;
        d.Run(
            [&](auto cmd)
            {
              d.Barrier(cmd, view.desc.image, old,
                        vk::ImageLayout::eShaderReadOnlyOptimal);
            });
        image.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
      }
      imageInfo =
          vk::DescriptorImageInfo(d.samplers.at(info.sampler), view.native,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);
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
    set.resources[{write.binding, write.arrayElement}] = write.resource;
  }
}
GraphicsPipelineHandle
VulkanRenderDevice::CreatePipeline(const GraphicsPipelineDesc& desc)
{
  auto& d = *m_impl;
  auto& layout = d.layouts.at(desc.layout);
  Impl::Pipeline pipeline;
  pipeline.desc = desc;
  pipeline.compute = static_cast<bool>(desc.computeShader);
  std::vector<vk::DescriptorSetLayout> setLayouts;
  for (auto h : layout.sets)
    setLayouts.push_back(d.setLayouts.at(h).native);
  std::vector<vk::ShaderCreateInfoEXT> infos;
  auto add = [&](ShaderModuleHandle handle, ShaderStageFlags stage)
  {
    auto& m = d.modules.at(handle);
    Require(m.stage == stage, "Pipeline shader stage mismatch");
    vk::ShaderCreateInfoEXT info;
    info.setStage(Stage(stage))
        .setCodeType(vk::ShaderCodeTypeEXT::eSpirv)
        .setCodeSize(m.words.size() * 4)
        .setPCode(m.words.data())
        .setPName(m.entry.c_str())
        .setSetLayouts(setLayouts);
    if (!pipeline.compute)
    {
      info.setFlags(vk::ShaderCreateFlagBitsEXT::eLinkStage);
      if (stage == ShaderStageFlags::Vertex)
        info.setNextStage(vk::ShaderStageFlagBits::eFragment);
    }
    infos.push_back(info);
  };
  if (pipeline.compute)
    add(desc.computeShader, ShaderStageFlags::Compute);
  else
  {
    Require(desc.vertexShader && desc.fragmentShader,
            "Graphics shaders are required");
    Samples(desc.sampleCount);
    add(desc.vertexShader, ShaderStageFlags::Vertex);
    add(desc.fragmentShader, ShaderStageFlags::Fragment);
  }
  auto result = d.device.createShadersEXT(infos, nullptr, d.dispatch);
  if (result.result != vk::Result::eSuccess)
  {
    for (auto shader : result.value)
      if (shader)
        d.device.destroyShaderEXT(shader, nullptr, d.dispatch);
    throw std::runtime_error("Vulkan shader-object compilation failed");
  }
  if (pipeline.compute)
    pipeline.shaders[2] = result.value.front();
  else
  {
    pipeline.shaders[0] = result.value[0];
    pipeline.shaders[1] = result.value[1];
  }
  auto h = d.Handle<GraphicsPipelineHandle>(d.nextPipeline);
  try
  {
    d.pipelines.emplace(h, std::move(pipeline));
  }
  catch (...)
  {
    for (auto shader : pipeline.shaders)
      if (shader)
        d.device.destroyShaderEXT(shader, nullptr, d.dispatch);
    throw;
  }
  return h;
}
void VulkanRenderDevice::DestroyPipeline(GraphicsPipelineHandle h)
{
  auto& d = *m_impl;
  auto it = d.pipelines.find(h);
  if (it == d.pipelines.end())
    return;
  WaitIdle();
  for (auto shader : it->second.shaders)
    if (shader)
      d.device.destroyShaderEXT(shader, nullptr, d.dispatch);
  d.pipelines.erase(it);
}
void VulkanRenderDevice::DestroyDescriptorSet(DescriptorSetHandle h)
{
  auto& d = *m_impl;
  auto it = d.sets.find(h);
  if (it == d.sets.end())
    return;
  WaitIdle();
  d.device.freeDescriptorSets(d.descriptorPool, it->second.native, d.dispatch);
  d.sets.erase(it);
}
void VulkanRenderDevice::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle h)
{
  auto& d = *m_impl;
  auto it = d.setLayouts.find(h);
  if (it == d.setLayouts.end())
    return;
  WaitIdle();
  d.device.destroyDescriptorSetLayout(it->second.native, nullptr, d.dispatch);
  d.setLayouts.erase(it);
}
void VulkanRenderDevice::DestroyPipelineLayout(PipelineLayoutHandle h)
{
  auto& d = *m_impl;
  auto it = d.layouts.find(h);
  if (it == d.layouts.end())
    return;
  WaitIdle();
  d.device.destroyPipelineLayout(it->second.native, nullptr, d.dispatch);
  d.layouts.erase(it);
}
void VulkanRenderDevice::DestroySampler(SamplerHandle h)
{
  auto& d = *m_impl;
  auto it = d.samplers.find(h);
  if (it == d.samplers.end())
    return;
  WaitIdle();
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

struct VulkanCommandList::Impl
{
  VulkanRenderDevice* owner;
  VulkanRenderDevice::Impl& d;
  vk::CommandPool pool;
  vk::CommandBuffer command;
  bool recording{}, complete{}, logicalRendering{}, nativeRendering{};
  RenderPassDesc pass;
  GraphicsPipelineHandle pipeline;
  std::uint32_t vertexStride{};
  struct State
  {
    vk::ImageLayout initial, current;
  };
  Map<ImageHandle, State> states;
  std::unordered_set<SwapchainHandle, Hash<SwapchainHandle>> touched, written;
  std::unordered_set<BufferHandle, Hash<BufferHandle>> buffersUsed;
  std::unordered_set<DescriptorSetHandle, Hash<DescriptorSetHandle>> setsUsed;
  std::unordered_set<GraphicsPipelineHandle, Hash<GraphicsPipelineHandle>>
      pipelinesUsed;
  std::map<unsigned, DescriptorSetHandle> boundSets;
  std::vector<DescriptorSetHandle> legacySets;
  SamplerHandle legacySampler;
  DescriptorSetHandle legacySet;
  GraphicsPipelineHandle legacyPipeline;
  explicit Impl(VulkanRenderDevice& device) : owner(&device), d(*device.m_impl)
  {
    pool = d.device.createCommandPool(
        vk::CommandPoolCreateInfo(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer, d.family),
        nullptr, d.dispatch);
    try
    {
      command = d.device
                    .allocateCommandBuffers(
                        vk::CommandBufferAllocateInfo(
                            pool, vk::CommandBufferLevel::ePrimary, 1),
                        d.dispatch)
                    .front();
    }
    catch (...)
    {
      d.device.destroyCommandPool(pool, nullptr, d.dispatch);
      throw;
    }
  }
  ~Impl()
  {
    for (auto h : legacySets)
      owner->DestroyDescriptorSet(h);
    if (legacySampler)
      owner->DestroySampler(legacySampler);
    d.device.destroyCommandPool(pool, nullptr, d.dispatch);
  }
  void RequireRecording()
  {
    Require(recording, "Begin command recording first");
  }
  void StopNative()
  {
    if (nativeRendering)
    {
      command.endRendering(d.dispatch);
      nativeRendering = false;
    }
  }
  void Transition(ImageHandle h, vk::ImageLayout next, bool write = false)
  {
    auto& image = d.images.at(h);
    if (image.swap)
    {
      auto* swap = d.Find(image.swap);
      Require(swap && swap->acquired && swap->images[swap->current] == h,
              "Swapchain image must be acquired");
      touched.insert(image.swap);
      if (write)
        written.insert(image.swap);
    }
    auto [it, _] = states.try_emplace(h, State{image.layout, image.layout});
    if (nativeRendering)
      StopNative();
    d.Barrier(command, h, it->second.current, next);
    it->second.current = next;
  }
  void PrepareSet(DescriptorSetHandle h)
  {
    auto& set = d.sets.at(h);
    setsUsed.insert(h);
    for (const auto& [_, resource] : set.resources)
    {
      if (const auto* image = std::get_if<DescriptorImageInfo>(&resource))
      {
        auto handle = d.views.at(image->image).desc.image;
        Require(!logicalRendering ||
                    (std::none_of(
                         pass.colorAttachments.begin(),
                         pass.colorAttachments.end(), [&](auto view)
                         { return d.views.at(view).desc.image == handle; }) &&
                     (!pass.depthAttachment ||
                      d.views.at(pass.depthAttachment).desc.image != handle)),
                "Attachment sampling feedback is not supported");
        auto it = states.find(handle);
        auto current = it == states.end() ? d.images.at(handle).layout
                                          : it->second.current;
        if (current != vk::ImageLayout::eShaderReadOnlyOptimal)
          Transition(handle, vk::ImageLayout::eShaderReadOnlyOptimal);
      }
      else
        buffersUsed.insert(std::get<DescriptorBufferInfo>(resource).buffer);
    }
  }
  void StartNative()
  {
    Require(logicalRendering, "Draw requires BeginRendering");
    if (nativeRendering)
      return;
    std::vector<vk::RenderingAttachmentInfo> colors;
    for (auto handle : pass.colorAttachments)
    {
      auto& view = d.views.at(handle);
      Transition(view.desc.image, vk::ImageLayout::eColorAttachmentOptimal,
                 true);
      vk::RenderingAttachmentInfo color;
      color.setImageView(view.native)
          .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
          .setLoadOp(vk::AttachmentLoadOp::eLoad)
          .setStoreOp(vk::AttachmentStoreOp::eStore);
      colors.push_back(color);
    }
    vk::RenderingAttachmentInfo depth;
    if (pass.depthAttachment)
    {
      auto& view = d.views.at(pass.depthAttachment);
      Transition(view.desc.image,
                 vk::ImageLayout::eDepthStencilAttachmentOptimal, true);
      depth.setImageView(view.native)
          .setImageLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
          .setLoadOp(vk::AttachmentLoadOp::eLoad)
          .setStoreOp(vk::AttachmentStoreOp::eStore);
    }
    vk::RenderingInfo rendering;
    rendering
        .setRenderArea(
            vk::Rect2D({pass.offsetX, pass.offsetY}, {pass.width, pass.height}))
        .setLayerCount(pass.layerCount)
        .setViewMask(pass.viewMask)
        .setColorAttachments(colors);
    if (pass.depthAttachment)
    {
      rendering.setPDepthAttachment(&depth);
      if (d.images.at(d.views.at(pass.depthAttachment).desc.image)
              .desc.format == ImageFormat::Depth24Stencil8)
        rendering.setPStencilAttachment(&depth);
    }
    command.beginRendering(rendering, d.dispatch);
    nativeRendering = true;
    // Engine coordinates are lower-left, +Y up, depth [0,1]. A negative
    // Vulkan viewport matches OpenGL without rewriting shader clip space.
    command.setViewportWithCount(
        vk::Viewport(static_cast<float>(pass.offsetX),
                     static_cast<float>(pass.offsetY + pass.height),
                     static_cast<float>(pass.width),
                     -static_cast<float>(pass.height), 0, 1),
        d.dispatch);
    command.setScissorWithCount(
        vk::Rect2D({pass.offsetX, pass.offsetY}, {pass.width, pass.height}),
        d.dispatch);
  }
  StencilMode stencilMode = StencilMode::Disabled;
  uint32_t stencilReference = 0;
  void GraphicsState(PrimitiveTopology topology)
  {
    auto& p = d.pipelines.at(pipeline);
    Require(!p.compute, "Graphics draw requires graphics shaders");
    Require(pass.viewMask == 0 || d.capabilities.multiView,
            "Multiview unavailable");
    std::vector<vk::VertexInputBindingDescription2EXT> bindings;
    std::vector<vk::VertexInputAttributeDescription2EXT> attributes;
    for (auto& b : p.desc.vertexLayout.bindings)
    {
      vk::VertexInputBindingDescription2EXT binding;
      binding.setBinding(b.binding)
          .setStride(b.binding == 0 && vertexStride ? vertexStride : b.stride)
          .setInputRate(b.inputRate == VertexInputRate::PerInstance
                            ? vk::VertexInputRate::eInstance
                            : vk::VertexInputRate::eVertex)
          .setDivisor(1);
      bindings.push_back(binding);
    }
    for (auto& a : p.desc.vertexLayout.attributes)
    {
      vk::VertexInputAttributeDescription2EXT attr;
      attr.setLocation(a.location)
          .setBinding(a.binding)
          .setFormat(VertexFormatNative(a.format))
          .setOffset(a.offset);
      attributes.push_back(attr);
    }
    command.setVertexInputEXT(bindings, attributes, d.dispatch);
    command.setPrimitiveTopology(Topology(topology), d.dispatch);
    command.setPrimitiveRestartEnable(false, d.dispatch);
    command.setRasterizerDiscardEnable(false, d.dispatch);
    command.setCullMode(vk::CullModeFlagBits::eNone, d.dispatch);
    command.setFrontFace(vk::FrontFace::eCounterClockwise, d.dispatch);
    command.setPolygonModeEXT(vk::PolygonMode::eFill, d.dispatch);
    command.setLineWidth(1, d.dispatch);
    command.setRasterizationSamplesEXT(Samples(p.desc.sampleCount), d.dispatch);
    const std::array<vk::SampleMask, 1> mask{~0u};
    command.setSampleMaskEXT(Samples(p.desc.sampleCount), mask.data(),
                             d.dispatch);
    command.setAlphaToCoverageEnableEXT(false, d.dispatch);
    command.setDepthBiasEnable(false, d.dispatch);
    command.setDepthTestEnable(static_cast<bool>(pass.depthAttachment) &&
                                   !pass.depthReadOnly,
                               d.dispatch);
    command.setDepthWriteEnable(static_cast<bool>(pass.depthAttachment) &&
                                    !pass.depthReadOnly,
                                d.dispatch);
    command.setDepthCompareOp(vk::CompareOp::eLess, d.dispatch);
    command.setDepthBoundsTestEnable(false, d.dispatch);
    command.setStencilTestEnable(stencilMode != StencilMode::Disabled,
                                 d.dispatch);
    command.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack, 0xff,
                                  d.dispatch);
    command.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                stencilMode == StencilMode::Write ? 0xff : 0,
                                d.dispatch);
    command.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                stencilReference, d.dispatch);
    command.setStencilOp(
        vk::StencilFaceFlagBits::eFrontAndBack, vk::StencilOp::eKeep,
        stencilMode == StencilMode::Write ? vk::StencilOp::eReplace
                                          : vk::StencilOp::eKeep,
        vk::StencilOp::eKeep,
        stencilMode == StencilMode::Equal ? vk::CompareOp::eEqual
                                          : vk::CompareOp::eAlways,
        d.dispatch);
    const std::vector<vk::Bool32> blend(pass.colorAttachments.size(),
                                        p.desc.blend.enabled);
    command.setColorBlendEnableEXT(0, blend, d.dispatch);
    if (p.desc.blend.enabled)
    {
      vk::ColorBlendEquationEXT equation;
      equation
          .setSrcColorBlendFactor(
              BlendFactorNative(p.desc.blend.sourceColorFactor))
          .setDstColorBlendFactor(
              BlendFactorNative(p.desc.blend.destinationColorFactor))
          .setColorBlendOp(BlendOperationNative(p.desc.blend.colorOperation))
          .setSrcAlphaBlendFactor(
              BlendFactorNative(p.desc.blend.sourceAlphaFactor))
          .setDstAlphaBlendFactor(
              BlendFactorNative(p.desc.blend.destinationAlphaFactor))
          .setAlphaBlendOp(BlendOperationNative(p.desc.blend.alphaOperation));
      const std::vector<vk::ColorBlendEquationEXT> equations(
          pass.colorAttachments.size(), equation);
      command.setColorBlendEquationEXT(0, equations, d.dispatch);
      const auto& value = p.desc.blend.constants;
      const std::array<float, 4> constants{
          std::clamp(value.x, 0.f, 1.f), std::clamp(value.y, 0.f, 1.f),
          std::clamp(value.z, 0.f, 1.f), std::clamp(value.w, 0.f, 1.f)};
      command.setBlendConstants(constants.data(), d.dispatch);
    }
    const std::vector<vk::ColorComponentFlags> colors(
        pass.colorAttachments.size(),
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    command.setColorWriteMaskEXT(0, colors, d.dispatch);
    Require(p.desc.colorFormats.empty() ||
                p.desc.colorFormats.size() == pass.colorAttachments.size(),
            "Pipeline color attachment count differs from rendering");
    for (size_t i = 0; i < pass.colorAttachments.size(); ++i)
    {
      const auto& image =
          d.images.at(d.views.at(pass.colorAttachments[i]).desc.image);
      Require(p.desc.sampleCount == image.desc.sampleCount,
              "Pipeline sample count differs from attachments");
      if (!p.desc.colorFormats.empty())
        Require(Format(p.desc.colorFormats[i]) == Format(image.desc.format),
                "Pipeline color format differs from attachment");
    }
    if (pass.depthAttachment)
      Require(p.desc.depthFormat ==
                  d.images.at(d.views.at(pass.depthAttachment).desc.image)
                      .desc.format,
              "Pipeline depth format differs from attachment");
  }
};

VulkanCommandList::VulkanCommandList(VulkanRenderDevice& device)
    : m_impl(std::make_unique<Impl>(device))
{
}
VulkanCommandList::~VulkanCommandList() = default;
const vk::CommandBuffer& VulkanCommandList::Native() const noexcept
{
  return m_impl->command;
}
void VulkanCommandList::Begin()
{
  auto& c = *m_impl;
  Require(!c.recording, "Command list already recording");
  Require(!c.complete, "Clear command list before re-recording");
  c.command.begin(vk::CommandBufferBeginInfo{}, c.d.dispatch);
  c.recording = true;
  vk::MemoryBarrier2 barrier;
  barrier.setSrcStageMask(vk::PipelineStageFlagBits2::eAllCommands)
      .setSrcAccessMask(vk::AccessFlagBits2::eMemoryWrite)
      .setDstStageMask(vk::PipelineStageFlagBits2::eAllCommands)
      .setDstAccessMask(vk::AccessFlagBits2::eMemoryRead |
                        vk::AccessFlagBits2::eMemoryWrite);
  vk::DependencyInfo dependency;
  dependency.setMemoryBarriers(barrier);
  c.command.pipelineBarrier2(dependency, c.d.dispatch);
}
void VulkanCommandList::End()
{
  auto& c = *m_impl;
  c.RequireRecording();
  Require(!c.logicalRendering, "End rendering before ending command list");
  c.command.end(c.d.dispatch);
  c.recording = false;
  c.complete = true;
}
void VulkanCommandList::Clear()
{
  auto& c = *m_impl;
  Require(!c.recording, "Cannot clear while recording");
  c.command.reset({}, c.d.dispatch);
  c.complete = false;
  c.states.clear();
  c.touched.clear();
  c.written.clear();
  c.buffersUsed.clear();
  c.setsUsed.clear();
  c.pipelinesUsed.clear();
  c.pipeline = {};
  c.vertexStride = 0;
  c.boundSets.clear();
}
void VulkanCommandList::BeginRendering(const RenderPassDesc& desc)
{
  auto& c = *m_impl;
  c.RequireRecording();
  Require(!c.logicalRendering, "Nested rendering is invalid");
  Require(desc.viewMask == 0,
          "Multiview is unavailable with Vulkan shader objects");
  c.pass = desc;
  c.stencilMode = StencilMode::Disabled;
  Require(!desc.colorAttachment || desc.colorAttachments.empty(),
          "Specify either colorAttachment or colorAttachments");
  if (c.pass.colorAttachments.empty() && desc.colorAttachment)
    c.pass.colorAttachments.push_back(desc.colorAttachment);
  Require(!c.pass.colorAttachments.empty() &&
              c.pass.colorAttachments.size() <=
                  c.d.properties.limits.maxColorAttachments,
          "Invalid color attachment count");
  auto& view = c.d.views.at(c.pass.colorAttachments.front());
  auto& image = c.d.images.at(view.desc.image);
  Require(image.desc.usage.Contains(ImageUsage::ColorAttachment),
          "Color attachment usage missing");
  if (!c.pass.width)
    c.pass.width = std::max(1u, image.desc.width >> view.desc.baseMipLevel);
  if (!c.pass.height)
    c.pass.height = std::max(1u, image.desc.height >> view.desc.baseMipLevel);
  Require(c.pass.offsetX >= 0 && c.pass.offsetY >= 0 &&
              c.pass.offsetX + c.pass.width <= image.desc.width &&
              c.pass.offsetY + c.pass.height <= image.desc.height,
          "Rendering rectangle exceeds attachment");
  for (auto handle : c.pass.colorAttachments)
  {
    auto& view = c.d.views.at(handle);
    auto& attachment = c.d.images.at(view.desc.image);
    Require(attachment.desc.usage.Contains(ImageUsage::ColorAttachment),
            "Color attachment usage missing");
    Require(attachment.desc.width == image.desc.width &&
                attachment.desc.height == image.desc.height &&
                attachment.desc.sampleCount == image.desc.sampleCount,
            "Color attachment sizes or sample counts differ");
    if (desc.clearColor)
    {
      c.Transition(view.desc.image, vk::ImageLayout::eTransferDstOptimal, true);
      const vk::ClearColorValue clear(
          std::array<float, 4>{desc.clearColorValue.x, desc.clearColorValue.y,
                               desc.clearColorValue.z, desc.clearColorAlpha});
      c.command.clearColorImage(
          attachment.native, vk::ImageLayout::eTransferDstOptimal, clear,
          vk::ImageSubresourceRange(
              vk::ImageAspectFlagBits::eColor, view.desc.baseMipLevel,
              view.desc.mipLevelCount, view.desc.baseArrayLayer,
              view.desc.arrayLayerCount),
          c.d.dispatch);
    }
  }
  if (desc.depthAttachment)
  {
    auto& dv = c.d.views.at(desc.depthAttachment);
    auto& depth = c.d.images.at(dv.desc.image);
    Require(depth.desc.usage.Contains(ImageUsage::DepthAttachment) ||
                depth.desc.usage.Contains(ImageUsage::DepthStencilAttachment),
            "Depth attachment usage missing");
    Require(depth.desc.width == image.desc.width &&
                depth.desc.height == image.desc.height &&
                depth.desc.sampleCount == image.desc.sampleCount,
            "Depth and color sizes differ");
    if (desc.clearDepth)
    {
      c.Transition(dv.desc.image, vk::ImageLayout::eTransferDstOptimal, true);
      c.command.clearDepthStencilImage(
          depth.native, vk::ImageLayout::eTransferDstOptimal,
          vk::ClearDepthStencilValue(1, 0),
          vk::ImageSubresourceRange(Aspect(depth.desc.format),
                                    dv.desc.baseMipLevel, dv.desc.mipLevelCount,
                                    dv.desc.baseArrayLayer,
                                    dv.desc.arrayLayerCount),
          c.d.dispatch);
    }
  }
  c.logicalRendering = true;
  c.StartNative();
}
void VulkanCommandList::SetStencil(StencilMode mode, uint32_t reference)
{
  auto& c = *m_impl;
  c.RequireRecording();
  Require(c.logicalRendering, "Stencil state requires rendering");
  Require(reference <= 255, "Stencil reference exceeds 8 bits");
  Require(mode == StencilMode::Disabled ||
              (c.pass.depthAttachment &&
               c.d.images.at(c.d.views.at(c.pass.depthAttachment).desc.image)
                       .desc.format == ImageFormat::Depth24Stencil8),
          "Stencil state requires a stencil attachment");
  c.stencilMode = mode;
  c.stencilReference = reference;
}
void VulkanCommandList::EndRendering()
{
  auto& c = *m_impl;
  c.RequireRecording();
  Require(c.logicalRendering, "No rendering scope");
  c.StartNative();
  c.StopNative();
  c.logicalRendering = false;
}
void VulkanCommandList::BindPipeline(GraphicsPipelineHandle h)
{
  auto& c = *m_impl;
  c.RequireRecording();
  auto& p = c.d.pipelines.at(h);
  c.pipeline = h;
  c.pipelinesUsed.insert(h);
  if (p.compute)
  {
    Require(!c.logicalRendering, "Compute shaders inside rendering");
    const std::array stages{vk::ShaderStageFlagBits::eCompute};
    const std::array shaders{p.shaders[2]};
    c.command.bindShadersEXT(stages, shaders, c.d.dispatch);
  }
  else
  {
    const std::array stages{vk::ShaderStageFlagBits::eVertex,
                            vk::ShaderStageFlagBits::eFragment};
    const std::array shaders{p.shaders[0], p.shaders[1]};
    c.command.bindShadersEXT(stages, shaders, c.d.dispatch);
  }
}
void VulkanCommandList::BindDescriptorSet(std::uint32_t index,
                                          DescriptorSetHandle h)
{
  auto& c = *m_impl;
  c.RequireRecording();
  auto& p = c.d.pipelines.at(c.pipeline);
  auto& layout = c.d.layouts.at(p.desc.layout);
  auto& set = c.d.sets.at(h);
  Require(index < layout.sets.size() && layout.sets[index] == set.layout,
          "Incompatible descriptor set layout");
  c.PrepareSet(h);
  c.boundSets[index] = h;
  c.command.bindDescriptorSets(p.compute ? vk::PipelineBindPoint::eCompute
                                         : vk::PipelineBindPoint::eGraphics,
                               layout.native, index, set.native, {},
                               c.d.dispatch);
}
void VulkanCommandList::BindVertexBuffer(BufferHandle h, std::uint32_t stride)
{
  auto& c = *m_impl;
  c.RequireRecording();
  auto& b = c.d.buffers.at(h);
  Require(b.desc.usage.Contains(BufferUsage::Vertex), "Vertex usage required");
  c.vertexStride = stride;
  c.buffersUsed.insert(h);
  const vk::DeviceSize offset = 0;
  c.command.bindVertexBuffers(0, b.native, offset, c.d.dispatch);
}
void VulkanCommandList::BindIndexBuffer(BufferHandle h)
{
  auto& c = *m_impl;
  c.RequireRecording();
  auto& b = c.d.buffers.at(h);
  Require(b.desc.usage.Contains(BufferUsage::Index), "Index usage required");
  c.buffersUsed.insert(h);
  c.command.bindIndexBuffer(b.native, 0, vk::IndexType::eUint32, c.d.dispatch);
}
void VulkanCommandList::Draw(PrimitiveTopology t, std::uint32_t count,
                             std::uint32_t first, std::uint32_t instances,
                             std::uint32_t firstInstance)
{
  auto& c = *m_impl;
  c.RequireRecording();
  c.StartNative();
  c.GraphicsState(t);
  c.command.draw(count, instances, first, firstInstance, c.d.dispatch);
}
void VulkanCommandList::DrawIndexed(std::uint32_t count)
{
  DrawIndexedRange(count, 0, 0);
}
void VulkanCommandList::DrawIndexedRange(std::uint32_t count,
                                         std::uint32_t first, std::int32_t base)
{
  auto& c = *m_impl;
  c.RequireRecording();
  c.StartNative();
  c.GraphicsState(c.d.pipelines.at(c.pipeline).desc.topology);
  c.command.drawIndexed(count, 1, first, base, 0, c.d.dispatch);
}
void VulkanCommandList::MultiDrawIndexedIndirect(BufferHandle buffer, std::uint32_t count)
{
  auto& c = *m_impl;
  c.RequireRecording();
  c.StartNative();
  c.GraphicsState(c.d.pipelines.at(c.pipeline).desc.topology);
  const auto native = c.d.buffers.at(buffer).native;
  const auto features = c.d.physical.getFeatures(c.d.dispatch);
  const auto limit = c.d.physical.getProperties(c.d.dispatch).limits.maxDrawIndirectCount;
  const std::uint32_t batch = features.multiDrawIndirect ? limit : 1;
  for (std::uint32_t first = 0; first < count;)
  {
    const auto n = std::min(batch, count - first);
    c.command.drawIndexedIndirect(native, vk::DeviceSize(first) * 20, n, 20, c.d.dispatch);
    first += n;
  }
}
void VulkanCommandList::Dispatch(std::uint32_t x, std::uint32_t y,
                                 std::uint32_t z)
{
  auto& c = *m_impl;
  c.RequireRecording();
  Require(!c.logicalRendering && c.d.pipelines.at(c.pipeline).compute,
          "Dispatch requires compute shaders outside rendering");
  c.command.dispatch(x, y, z, c.d.dispatch);
}
void VulkanCommandList::BlitImage(ImageHandle src, ImageHandle dst,
                                  const BlitRegion& region, ImageFilter filter)
{
  auto& c = *m_impl;
  c.RequireRecording();
  Require(!c.logicalRendering && src != dst,
          "Blit requires distinct images outside rendering");
  auto& source = c.d.images.at(src);
  auto& destination = c.d.images.at(dst);
  Require(source.desc.sampleCount == 1 && destination.desc.sampleCount == 1,
          "Blit requires single-sampled images");
  Require(*region.srcSubresource.aspectMask ==
                  *region.dstSubresource.aspectMask &&
              !region.srcSubresource.aspectMask.Empty(),
          "Blit aspects must match");
  Require(region.srcSubresource.layerCount &&
              region.srcSubresource.layerCount ==
                  region.dstSubresource.layerCount,
          "Blit layer counts must match");
  const auto srcAspect = Aspect(region.srcSubresource.aspectMask);
  Require((srcAspect & Aspect(source.desc.format)) == srcAspect &&
              (srcAspect & Aspect(destination.desc.format)) == srcAspect,
          "Blit aspect differs from image format");
  if (!(srcAspect & vk::ImageAspectFlagBits::eColor))
    Require(
        filter == ImageFilter::Nearest &&
            source.desc.format == destination.desc.format,
        "Depth/stencil blits require matching formats and nearest filtering");
  if (source.swap)
    Require(source.desc.usage.Contains(ImageUsage::TransferSource),
            "Swapchain image does not support transfer-source use");
  c.Transition(src, vk::ImageLayout::eTransferSrcOptimal);
  c.Transition(dst, vk::ImageLayout::eTransferDstOptimal, true);
  const auto layers = [](const ImageSubresourceLayers& s)
  {
    return vk::ImageSubresourceLayers(Aspect(s.aspectMask), s.mipLevel,
                                      s.baseArrayLayer, s.layerCount);
  };
  vk::ImageBlit2 blit;
  blit.setSrcSubresource(layers(region.srcSubresource))
      .setDstSubresource(layers(region.dstSubresource));
  for (unsigned i = 0; i < 2; ++i)
  {
    blit.srcOffsets[i] = vk::Offset3D(static_cast<int>(region.srcOffsets[i].x),
                                      static_cast<int>(region.srcOffsets[i].y),
                                      static_cast<int>(region.srcOffsets[i].z));
    blit.dstOffsets[i] = vk::Offset3D(static_cast<int>(region.dstOffsets[i].x),
                                      static_cast<int>(region.dstOffsets[i].y),
                                      static_cast<int>(region.dstOffsets[i].z));
  }
  vk::BlitImageInfo2 info;
  info.setSrcImage(source.native)
      .setSrcImageLayout(vk::ImageLayout::eTransferSrcOptimal)
      .setDstImage(destination.native)
      .setDstImageLayout(vk::ImageLayout::eTransferDstOptimal)
      .setRegions(blit)
      .setFilter(filter == ImageFilter::Linear ? vk::Filter::eLinear
                                               : vk::Filter::eNearest);
  c.command.blitImage2(info, c.d.dispatch);
}
void VulkanCommandList::BindTexture(std::uint32_t slot, ImageHandle image)
{
  auto& c = *m_impl;
  c.RequireRecording();
  auto& p = c.d.pipelines.at(c.pipeline);
  auto& layout = c.d.layouts.at(p.desc.layout);
  Require(!layout.sets.empty(),
          "BindTexture requires a descriptor set 0 layout");
  auto& setLayout = c.d.setLayouts.at(layout.sets.front());
  const auto& binding = setLayout.bindings.at(slot);
  Require(binding.type == DescriptorType::CombinedImageSampler &&
              binding.count == 1,
          "BindTexture maps slot to set 0 sampled-image binding");
  auto previous =
      c.boundSets.contains(0) ? c.boundSets.at(0) : DescriptorSetHandle{};
  c.legacySet = c.owner->CreateDescriptorSet({layout.sets.front()});
  c.legacySets.push_back(c.legacySet);
  if (previous && c.d.sets.at(previous).layout == layout.sets.front())
  {
    const auto old = c.d.sets.at(previous);
    for (const auto& [key, value] : old.resources)
    {
      vk::CopyDescriptorSet copy;
      copy.setSrcSet(old.native)
          .setSrcBinding(key.first)
          .setSrcArrayElement(key.second)
          .setDstSet(c.d.sets.at(c.legacySet).native)
          .setDstBinding(key.first)
          .setDstArrayElement(key.second)
          .setDescriptorCount(1);
      c.d.device.updateDescriptorSets({}, copy, c.d.dispatch);
    }
    c.d.sets.at(c.legacySet).resources = old.resources;
  }
  if (!c.legacySampler)
    c.legacySampler = c.owner->CreateSampler({});
  auto existing =
      std::find_if(c.d.views.begin(), c.d.views.end(), [&](const auto& item)
                   { return item.second.desc.image == image; });
  Require(existing != c.d.views.end(), "BindTexture requires an image view");
  c.Transition(image, vk::ImageLayout::eShaderReadOnlyOptimal);
  auto& set = c.d.sets.at(c.legacySet);
  vk::DescriptorImageInfo info(c.d.samplers.at(c.legacySampler),
                               existing->second.native,
                               vk::ImageLayout::eShaderReadOnlyOptimal);
  vk::WriteDescriptorSet write;
  write.setDstSet(set.native)
      .setDstBinding(slot)
      .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
      .setImageInfo(info);
  c.d.device.updateDescriptorSets(write, {}, c.d.dispatch);
  set.resources[{slot, 0}] =
      DescriptorImageInfo{existing->first, c.legacySampler};
  BindDescriptorSet(0, c.legacySet);
}
std::unique_ptr<IRenderCommandList>
VulkanRenderDevice::CreateCommandList(QueueType)
{
  return std::make_unique<VulkanCommandList>(*this);
}
void VulkanRenderDevice::Submit(const IRenderCommandList& list)
{
  const auto* native = dynamic_cast<const VulkanCommandList*>(&list);
  Require(native && native->m_impl->owner == this, "Cross-device command list");
  auto& c = *native->m_impl;
  auto& d = *m_impl;
  Require(c.complete && !c.recording,
          "Submit requires completed native recording");
  for (const auto& [h, state] : c.states)
    Require(d.images.contains(h) && d.images.at(h).layout == state.initial,
            "Image changed after command recording; record command lists in "
            "submission order");
  for (auto h : c.buffersUsed)
    Require(d.buffers.contains(h), "Recorded buffer was destroyed");
  for (auto h : c.pipelinesUsed)
    Require(d.pipelines.contains(h), "Recorded shaders were destroyed");
  for (auto h : c.setsUsed)
  {
    const auto& set = d.sets.at(h);
    for (const auto& [binding, desc] : d.setLayouts.at(set.layout).bindings)
      for (unsigned i = 0; i < desc.count; ++i)
        Require(set.resources.contains({binding, i}),
                "Descriptor contains an uninitialized binding");
  }
  std::vector<vk::Semaphore> waits;
  for (auto h : c.touched)
  {
    auto* swap = d.Find(h);
    Require(swap && swap->acquired, "Acquired swapchain was destroyed");
    if (swap->waitPending)
      waits.push_back(swap->available);
  }
  d.Execute(c.command, waits);
  for (const auto& [h, state] : c.states)
    d.images.at(h).layout = state.current;
  for (auto h : c.touched)
    d.Find(h)->waitPending = false;
  for (auto h : c.written)
    d.Find(h)->ready = true;
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
    if (d.physical.getSurfaceSupportKHR(d.family, surface, d.dispatch) &&
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
VulkanRenderDevice::CreateSwapchain(Window& window,
                                    const SwapchainConfig& config)
{
  auto& d = *m_impl;
  Require(d.swapchainEnabled && config.minImageCount > 0,
          "Vulkan swapchain unavailable or invalid image count");
  auto state = std::make_unique<Impl::Swap>();
  state->window = static_cast<GLFWwindow*>(window.NativeHandle());
  state->config = config;
  // The platform adapter exposes a C signature; the surface itself is Hpp.
  const auto result = static_cast<vk::Result>(glfwCreateWindowSurface(
      static_cast<vk::Instance::CType>(d.instance), state->window, nullptr,
      reinterpret_cast<vk::SurfaceKHR::CType*>(&state->surface)));
  Require(result == vk::Result::eSuccess,
          ("GLFW Vulkan surface creation failed: " + vk::to_string(result))
              .c_str());
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
    Require(d.physical.getSurfaceSupportKHR(d.family, swap.surface, d.dispatch),
            "The single graphics queue cannot present to this surface");
    swap.available = d.device.createSemaphore({}, nullptr, d.dispatch);
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
      swap->waitPending = true;
      swap->ready = false;
      return {swap->images[swap->current], swap->views[swap->current]};
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
  if (swap->waitPending)
    d.Run([](auto) {}, {swap->available});
  d.DropSwapImages(*swap);
  if (swap->native)
    d.device.destroySwapchainKHR(swap->native, nullptr, d.dispatch);
  if (swap->available)
    d.device.destroySemaphore(swap->available, nullptr, d.dispatch);
  if (swap->surface)
    d.instance.destroySurfaceKHR(swap->surface, nullptr, d.dispatch);
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
void VulkanRenderDevice::Present(SwapchainHandle handle)
{
  auto& d = *m_impl;
  auto* swap = d.Find(handle);
  Require(swap && swap->acquired && swap->ready,
          "Present requires submitted swapchain work");
  auto image = swap->images[swap->current];
  auto& state = d.images.at(image);
  std::vector<vk::Semaphore> waits;
  if (swap->waitPending)
    waits.push_back(swap->available);
  d.Run(
      [&](auto cmd)
      { d.Barrier(cmd, image, state.layout, vk::ImageLayout::ePresentSrcKHR); },
      waits, {swap->finished.at(swap->current)});
  state.layout = vk::ImageLayout::ePresentSrcKHR;
  swap->waitPending = false;
  vk::PresentInfoKHR present;
  present.setWaitSemaphores(swap->finished.at(swap->current))
      .setSwapchains(swap->native)
      .setImageIndices(swap->current);
  bool outOfDate = false;
  try
  {
    const auto result = d.queue.presentKHR(present, d.dispatch);
    outOfDate = result == vk::Result::eSuboptimalKHR;
  }
  catch (const vk::OutOfDateKHRError&)
  {
    outOfDate = true;
  }
  d.queue.waitIdle(d.dispatch);
  swap->acquired = false;
  swap->ready = false;
  if (outOfDate)
    d.MakeSwapImages(handle);
}

#ifdef LIGHTHOUSE_IMGUI
void VulkanRenderDevice::InitializeImGui(WindowManager& windows)
{
  auto& d = *m_impl;
  auto* context = ImGui::GetCurrentContext();
  if (d.gui.contains(context))
    return;
  Require(d.swapchainEnabled, "ImGui viewports need presentation support");
  windows.Initialize();
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
  auto* host =
      glfwCreateWindow(1280, 720, "Lighthouse ImGui host", nullptr, nullptr);
  Require(host, "Failed to create ImGui host");
  bool platform = false, renderer = false;
  try
  {
    Require(ImGui_ImplGlfw_InitForVulkan(host, true),
            "ImGui GLFW initialization failed");
    platform = true;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = vk::ApiVersion14;
    info.Instance = static_cast<vk::Instance::CType>(d.instance);
    info.PhysicalDevice = static_cast<vk::PhysicalDevice::CType>(d.physical);
    info.Device = static_cast<vk::Device::CType>(d.device);
    info.QueueFamily = d.family;
    info.Queue = static_cast<vk::Queue::CType>(d.queue);
    info.DescriptorPoolSize = 1024;
    info.MinImageCount = 2;
    info.ImageCount = 3;
    info.UseDynamicRendering = true;
    vk::PipelineRenderingCreateInfo rendering;
    const auto format = vk::Format::eR8G8B8A8Unorm;
    rendering.setColorAttachmentFormats(format);
    info.PipelineInfoMain.MSAASamples =
        static_cast<decltype(info.PipelineInfoMain.MSAASamples)>(
            vk::SampleCountFlagBits::e1);
    info.PipelineInfoForViewports.MSAASamples =
        info.PipelineInfoMain.MSAASamples;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = *reinterpret_cast<
        const decltype(info.PipelineInfoMain.PipelineRenderingCreateInfo)*>(
        &rendering);
    Require(ImGui_ImplVulkan_Init(&info),
            "ImGui Vulkan renderer initialization failed");
    renderer = true;
    d.gui.emplace(context, Impl::Gui{host});
  }
  catch (...)
  {
    if (renderer)
      ImGui_ImplVulkan_Shutdown();
    if (platform)
      ImGui_ImplGlfw_Shutdown();
    glfwDestroyWindow(host);
    glfwDefaultWindowHints();
    throw;
  }
  glfwDefaultWindowHints();
}
void VulkanRenderDevice::ShutdownImGui() noexcept
{
  auto& d = *m_impl;
  auto it = d.gui.find(ImGui::GetCurrentContext());
  if (it == d.gui.end())
    return;
  try
  {
    WaitIdle();
    ImGui::DestroyPlatformWindows();
    Detail::RemoveImGuiGlfwCallbacks(ImGui::GetCurrentContext());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    glfwDestroyWindow(it->second.host);
    d.gui.erase(it);
  }
  catch (...)
  {
  }
}
void VulkanRenderDevice::BeginImGuiFrame()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
}
void VulkanRenderDevice::RenderImGuiViewports()
{
  // ImGui's GLFW adapter inherits CLIENT_API rather than setting it for
  // secondary windows. Defaults request OpenGL, whose windows cannot acquire
  // Vulkan surfaces. Reapply this before every update (other views can change
  // the process-wide GLFW hints between frames).
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  try
  {
    ImGui::UpdatePlatformWindows();
    Detail::RouteImGuiGlfwCallbacks();
    ImGui::RenderPlatformWindowsDefault();
  }
  catch (...)
  {
    glfwDefaultWindowHints();
    throw;
  }
  glfwDefaultWindowHints();
}
void VulkanRenderDevice::EndImGuiFrame() noexcept {}
#endif
} // namespace Lighthouse
