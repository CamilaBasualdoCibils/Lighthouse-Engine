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
  explicit OpenGLRenderDevice(std::shared_ptr<Detail::OpenGLDeviceGroup> group,
                              const DeviceConfig& config = {});
  ~OpenGLRenderDevice() override;
  OpenGLRenderDevice(const OpenGLRenderDevice&) = delete;
  OpenGLRenderDevice& operator=(const OpenGLRenderDevice&) = delete;

  bool RequiresEGLWindow() const noexcept override
  {
    return true;
  }

  ImageHandle CreateImage(const ImageDesc& description) override;
  std::vector<std::byte> ReadImage(ImageHandle) override;
  ImageViewHandle CreateImageView(const ImageViewDesc& description) override;
  BufferHandle CreateBuffer(const BufferDesc& description) override;
  void UpdateBuffer(BufferHandle, std::uint64_t,
                    std::span<const std::byte>) override;
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
  CreateCommandList(QueueType type) override;
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
  SwapchainHandle CreateSwapchain(Window& window,
                                  const SwapchainConfig& config) override;
  AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) override;
  bool DestroySwapchain(SwapchainHandle swapchain) override;
  bool IsValid(SwapchainHandle swapchain) const noexcept override;
  void Submit(const IRenderCommandList& commandList) override;
  void Present(SwapchainHandle swapchain) override;
  void WaitIdle() override;
  ImageViewHandle AcquiredImageView(SwapchainHandle swapchain) const noexcept;
#ifdef LIGHTHOUSE_IMGUI
  void InitializeImGui(WindowManager& windows) override;
  void ShutdownImGui() noexcept override;
  void BeginImGuiFrame() override;
  void RenderImGuiViewports() override;
  void EndImGuiFrame() noexcept override;
#endif

private:
  friend class OpenGLCommandList;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
