#pragma once

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#endif

#include <memory>

namespace Lighthouse
{

class Window;

// Vulkan 1.4-only, single-queue device using dynamic rendering and shader
// objects.
class VulkanRenderDevice final : public IRenderDevice
#ifdef LIGHTHOUSE_IMGUI
    ,
                                 public IImGuiDevice
#endif
{
public:
  using IRenderDevice::CreateShaderModule;
  explicit VulkanRenderDevice(const DeviceConfig& config = {});
  ~VulkanRenderDevice() override;
  VulkanRenderDevice(const VulkanRenderDevice&) = delete;
  VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

  ImageHandle CreateImage(const ImageDesc&) override;
  std::vector<std::byte> ReadImage(ImageHandle) override;
  ImageViewHandle CreateImageView(const ImageViewDesc&) override;
  BufferHandle CreateBuffer(const BufferDesc&) override;
  void UpdateBuffer(BufferHandle, std::uint64_t,
                    std::span<const std::byte>) override;
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
  std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override;
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
  SwapchainHandle CreateSwapchain(Window& window,
                                  const SwapchainConfig& config) override;
  AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) override;
  bool DestroySwapchain(SwapchainHandle) override;
  bool IsValid(SwapchainHandle) const noexcept override;
  void Submit(const IRenderCommandList&) override;
  void Present(SwapchainHandle swapchain) override;
  void WaitIdle() override;
#ifdef LIGHTHOUSE_IMGUI
  void InitializeImGui(WindowManager& windows) override;
  void ShutdownImGui() noexcept override;
  void BeginImGuiFrame() override;
  void RenderImGuiViewports() override;
  void EndImGuiFrame() noexcept override;
#endif

private:
  friend class VulkanCommandList;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
