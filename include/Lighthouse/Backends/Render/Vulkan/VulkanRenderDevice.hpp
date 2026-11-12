#pragma once

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#endif

#include <memory>

namespace Lighthouse
{

class Window;

// Vulkan 1.4 device using core dynamic rendering and VkPipeline objects.
class VulkanRenderDevice final : public IRenderDevice
#ifdef LIGHTHOUSE_IMGUI
    ,
                                 public IImGuiDevice
#endif
{
public:
  using IRenderDevice::CreateShaderModule;
  explicit VulkanRenderDevice(
      const RenderDeviceCreateInfo& config,
      std::span<const std::pair<SurfaceHandle, Window*>> surfaces = {});
  ~VulkanRenderDevice() override;
  VulkanRenderDevice(const VulkanRenderDevice&) = delete;
  VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

  std::vector<SwapchainFormat>
  EnumerateAvailableSwapchainFormats(Window&) override;
  std::optional<SwapchainInfo> GetSwapchainInfo(SwapchainHandle) const override;

  ImageHandle CreateImage(const ImageDesc&) override;
  std::vector<std::byte> ReadImage(ImageHandle) override;
  ImageLayout GetImageLayout(ImageHandle) const override;
  ImageViewHandle CreateImageView(const ImageViewDesc&) override;
  BufferHandle CreateBuffer(const BufferDesc&) override;
  IRenderQueue* GetQueue(std::uint32_t index) override;
  FenceHandle CreateFence(bool signaled = false) override;
  void DestroyFence(FenceHandle) override;
  void ResetFence(FenceHandle) override;
  bool GetFenceStatus(FenceHandle) override;
  bool WaitForFence(FenceHandle, std::uint64_t timeout = UINT64_MAX) override;
  SemaphoreHandle CreateSemaphore(SemaphoreType type = SemaphoreType::Binary,
                                  std::uint64_t initialValue = 0) override;
  void DestroySemaphore(SemaphoreHandle) override;
  std::uint64_t GetSemaphoreCounterValue(SemaphoreHandle) override;
  void SignalSemaphore(SemaphoreHandle, std::uint64_t value) override;
  bool WaitSemaphore(SemaphoreHandle, std::uint64_t value,
                     std::uint64_t timeout = UINT64_MAX) override;
  std::span<std::byte> MapMemory(BufferHandle) override;
  void UnmapMemory(BufferHandle) override;
  void FlushMappedMemoryRange(BufferHandle, std::uint64_t offset,
                              std::uint64_t size) override;
  void InvalidateMappedMemoryRange(BufferHandle, std::uint64_t offset,
                                   std::uint64_t size) override;
  std::optional<BufferMetadata>
      GetBufferDesc(BufferHandle) const noexcept override;
  bool IsValid(BufferHandle) const noexcept override;
  bool IsValid(DescriptorSetLayoutHandle) const noexcept override;
  bool IsValid(DescriptorSetHandle) const noexcept override;
  bool IsValid(PipelineLayoutHandle) const noexcept override;
  bool IsValid(SamplerHandle) const noexcept override;
  ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override;
  DescriptorSetLayoutHandle
  CreateDescriptorSetLayout(const DescriptorSetLayoutDesc&) override;
  DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc&) override;
  PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc&) override;
  SamplerHandle CreateSampler(const SamplerDesc&) override;
  void UpdateDescriptorSet(DescriptorSetHandle,
                           std::span<const DescriptorWrite>) override;
  GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override;
  std::unique_ptr<IRenderCommandList>
  CreateCommandList(std::uint32_t queueIndex) override;
  void DestroyImage(ImageHandle) override;
  void DestroyImageView(ImageViewHandle) override;
  void DestroyBuffer(BufferHandle) override;
  void DestroyPipeline(GraphicsPipelineHandle) override;
  void DestroyModule(ShaderModuleHandle) override;
  void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle) override;
  void DestroyDescriptorSet(DescriptorSetHandle) override;
  void DestroyPipelineLayout(PipelineLayoutHandle) override;
  void DestroySampler(SamplerHandle) override;
  RenderCapabilities GetCapabilities() const override;
  SurfaceHandle CreateSurface(Window&) override;
  void DestroySurface(SurfaceHandle) override;
  SwapchainHandle CreateSwapchain(SurfaceHandle surface,
                                  const SwapchainConfig& config) override;
  AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) override;
  bool DestroySwapchain(SwapchainHandle) override;
  bool IsValid(SwapchainHandle) const noexcept override;

  void WaitIdle() override;
#ifdef LIGHTHOUSE_IMGUI
  void InitializeImGui(WindowManager& windows, WindowView* host = nullptr) override;
  void RenderImGuiHost(WindowView& host) override;
  void ShutdownImGui() noexcept override;
  void BeginImGuiFrame() override;
  void RenderImGuiViewports() override;
  void EndImGuiFrame() noexcept override;
#endif

private:
  class Queue;
  std::vector<std::unique_ptr<IRenderQueue>> m_queues;
  friend class VulkanCommandList;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
