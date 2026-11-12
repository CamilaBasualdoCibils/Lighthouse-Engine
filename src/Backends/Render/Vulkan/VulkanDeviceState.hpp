#pragma once
#include <Lighthouse/Backends/Render/Vulkan/VulkanCommandList.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanRenderDevice.hpp>
#include <Lighthouse/Engine/Assets/ImageData.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <numeric>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/Detail/ImGuiGlfwRouting.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
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

#include "VulkanConversions.hpp"
namespace Lighthouse
{
using namespace VulkanDetail;
class VulkanRenderDevice::Queue final : public IRenderQueue
{
  VulkanRenderDevice& device;

public:
  vk::Queue native;
  std::uint32_t family;
  Flags<RenderQueueCapability> capabilities;

public:
  Queue(VulkanRenderDevice& d, vk::Queue q, std::uint32_t f,
        Flags<RenderQueueCapability> caps)
      : device(d), native(q), family(f), capabilities(caps)
  {
  }
  Flags<RenderQueueCapability> GetCapabilities() const noexcept override;
  void Submit(const QueueSubmitInfo&, FenceHandle) override;
  void Present(SwapchainHandle, std::span<const SemaphoreHandle>) override;
  void WaitIdle() override;
};

struct VulkanRenderDevice::Impl
{
  vk::detail::DispatchLoaderDynamic dispatch{vkGetInstanceProcAddr};
  vk::Instance instance;
  vk::PhysicalDevice physical;
  vk::PhysicalDeviceProperties properties;
  vk::Device device;
  vk::Queue queue;
  struct QueueEntry
  {
    vk::Queue native;
    std::uint32_t family;
    Flags<RenderQueueCapability> capabilities;
  };
  std::vector<QueueEntry> queues;
  std::vector<std::uint32_t> queueFamilies;
  struct Surface
  {
    vk::SurfaceKHR native;
    GLFWwindow* window;
  };
  Map<SurfaceHandle, Surface> surfaces;
  unsigned nextSurface = 1;
  std::uint32_t family{};
  VmaAllocator allocator{};
  vk::CommandPool immediatePool;
  std::map<unsigned, vk::CommandPool> commandPools;
  vk::Fence fence;
  unsigned nextFence = 1, nextSemaphore = 1;
  Map<FenceHandle, vk::Fence> fences;
  Map<SemaphoreHandle, vk::Semaphore> semaphores;
  vk::DescriptorPool descriptorPool;
  RenderCapabilities capabilities;
  std::uint64_t owner{};
  std::uint32_t nextImage{1}, nextView{1}, nextBuffer{1}, nextModule{1},
      nextSetLayout{1}, nextSet{1}, nextLayout{1}, nextSampler{1},
      nextPipeline{1};
  bool swapchainEnabled{};
  std::array<bool, 3> updateAfterBind{};
  struct Buffer
  {
    vk::Buffer native;
    VmaAllocation allocation{};
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
    vk::ShaderModule native;
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
  };
  struct Layout
  {
    vk::PipelineLayout native;
  };
  struct Pipeline
  {
    vk::Pipeline native;
    PipelineLayoutHandle layout;
    bool compute{};
  };
  struct Swap
  {
    GLFWwindow* window{};
    vk::SurfaceKHR surface;
    SurfaceHandle surfaceHandle;
    vk::SwapchainKHR native;
    vk::SurfaceFormatKHR format;
    vk::Extent2D extent;
    std::vector<ImageHandle> images;
    std::vector<ImageViewHandle> views;
    vk::Semaphore available;
    SemaphoreHandle availableHandle;
    std::vector<SemaphoreHandle> finishedHandles;
    std::vector<vk::Semaphore> finished;
    std::uint32_t current{}, generation{1};
    bool acquired{};
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
    bool engineHost{};
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
  void Init(const RenderDeviceCreateInfo& config, std::uint64_t ownerId,
            std::span<const std::pair<SurfaceHandle, Window*>> targets);
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
    for (auto handle : swap.finishedHandles)
      semaphores.erase(handle);
    swap.finishedHandles.clear();
    swap.finished.clear();
    swap.views.clear();
    swap.images.clear();
  }
};

} // namespace Lighthouse
