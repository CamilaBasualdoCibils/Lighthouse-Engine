#pragma once

#include <cstdint>
#include <memory>

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#endif

namespace Lighthouse
{

namespace Detail
{
struct OpenGLDeviceGroup;
}
class OpenGLCommandList;

// OpenGL 4.5 device backed by an EGL context and window surface.
class OpenGLRenderDevice final : public IRenderDevice
#ifdef LIGHTHOUSE_IMGUI
    ,
                                 public IImGuiDevice
#endif
{
public:
  using IRenderDevice::CreateShaderModule;
  explicit OpenGLRenderDevice(
      std::shared_ptr<Detail::OpenGLDeviceGroup> group,
      const RenderDeviceCreateInfo& config,
      std::span<const std::pair<SurfaceHandle, Window*>> surfaces = {});
  ~OpenGLRenderDevice() override;
  OpenGLRenderDevice(const OpenGLRenderDevice&) = delete;
  OpenGLRenderDevice& operator=(const OpenGLRenderDevice&) = delete;

  bool RequiresEGLWindow() const noexcept override
  {
    return true;
  }

  std::vector<SwapchainFormat>
  EnumerateAvailableSwapchainFormats(Window&) override;
  std::optional<SwapchainInfo> GetSwapchainInfo(SwapchainHandle) const override;

  ImageHandle CreateImage(const ImageDesc& description) override;
  std::vector<std::byte> ReadImage(ImageHandle) override;
  ImageViewHandle CreateImageView(const ImageViewDesc& description) override;
  BufferHandle CreateBuffer(const BufferDesc& description) override;
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
  GetBufferDesc(BufferHandle handle) const noexcept override;
  bool IsValid(BufferHandle handle) const noexcept override;
  bool IsValid(DescriptorSetLayoutHandle handle) const noexcept override;
  bool IsValid(DescriptorSetHandle handle) const noexcept override;
  bool IsValid(PipelineLayoutHandle handle) const noexcept override;
  bool IsValid(SamplerHandle handle) const noexcept override;
  ShaderModuleHandle
  CreateShaderModule(const ShaderModuleDesc& description) override;
  DescriptorSetLayoutHandle CreateDescriptorSetLayout(
      const DescriptorSetLayoutDesc& description) override;
  DescriptorSetHandle
  CreateDescriptorSet(const DescriptorSetDesc& description) override;
  PipelineLayoutHandle
  CreatePipelineLayout(const PipelineLayoutDesc& description) override;
  SamplerHandle CreateSampler(const SamplerDesc& description) override;
  void UpdateDescriptorSet(DescriptorSetHandle set,
                           std::span<const DescriptorWrite> writes) override;
  GraphicsPipelineHandle
  CreatePipeline(const GraphicsPipelineDesc& description) override;
  std::unique_ptr<IRenderCommandList>
  CreateCommandList(std::uint32_t queueIndex) override;
  void DestroyImage(ImageHandle handle) override;
  void DestroyImageView(ImageViewHandle handle) override;
  void DestroyBuffer(BufferHandle handle) override;
  void DestroyPipeline(GraphicsPipelineHandle handle) override;
  void DestroyModule(ShaderModuleHandle handle) override;
  void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) override;
  void DestroyDescriptorSet(DescriptorSetHandle handle) override;
  void DestroyPipelineLayout(PipelineLayoutHandle handle) override;
  void DestroySampler(SamplerHandle handle) override;
  RenderCapabilities GetCapabilities() const override;
  SurfaceHandle CreateSurface(Window&) override;
  void DestroySurface(SurfaceHandle) override;
  SwapchainHandle CreateSwapchain(SurfaceHandle surface,
                                  const SwapchainConfig& config) override;
  AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) override;
  bool DestroySwapchain(SwapchainHandle swapchain) override;
  bool IsValid(SwapchainHandle swapchain) const noexcept override;

  void WaitIdle() override;
  ImageViewHandle AcquiredImageView(SwapchainHandle swapchain) const noexcept;
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
  friend class OpenGLCommandList;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
