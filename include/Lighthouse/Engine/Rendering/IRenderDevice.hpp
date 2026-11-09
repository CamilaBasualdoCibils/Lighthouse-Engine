#pragma once

#include <Lighthouse/Engine/Rendering/IRenderQueue.hpp>
#include <atomic>
#include <memory>
#include <optional>

#include <Lighthouse/Engine/Assets/Shader.hpp>
#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

namespace Lighthouse
{

class Window;

class IRenderDevice
{
public:
  IRenderDevice() : m_deviceId(NextDeviceId()) {}
  virtual ~IRenderDevice() = default;
  IRenderDevice(const IRenderDevice&) = delete;
  IRenderDevice& operator=(const IRenderDevice&) = delete;

  std::uint64_t DeviceId() const noexcept
  {
    return m_deviceId;
  }

  // Native window requirement used by RenderGraphComposer when creating a window view.
  virtual bool RequiresEGLWindow() const noexcept
  {
    return false;
  }

  // Queries a real native surface. The result describes presentation support,
  // not the monitor's peak brightness or whether the desktop has HDR enabled.
  virtual std::vector<SwapchainFormat>
  EnumerateAvailableSwapchainFormats(Window&)
  {
    return {}; // Backends must explicitly advertise their presentation support.
  }
  virtual std::optional<SwapchainInfo> GetSwapchainInfo(SwapchainHandle) const
  {
    return std::nullopt;
  }

  // Platform surface creation is exposed here because this architecture owns
  // the Vulkan instance/EGL display inside the render device.
  virtual SurfaceHandle CreateSurface(Window&) = 0;
  virtual void DestroySurface(SurfaceHandle) = 0;
  virtual SwapchainHandle CreateSwapchain(SurfaceHandle,
                                          const SwapchainConfig&) = 0;
  // Acquires an image for this swapchain. Other swapchains may remain acquired.
  // The returned attachments identify the destination of recorded commands.
  virtual AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) = 0;
  virtual bool DestroySwapchain(SwapchainHandle) = 0;
  virtual bool IsValid(SwapchainHandle) const noexcept = 0;

  virtual ImageHandle CreateImage(const ImageDesc& description) = 0;
  // Blocking RGBA8 readback for diagnostics and headless consumers.
  virtual std::vector<std::byte> ReadImage(ImageHandle)
  {
    throw std::logic_error("Image readback unsupported");
  }
  virtual ImageViewHandle CreateImageView(const ImageViewDesc& description) = 0;
  virtual BufferHandle CreateBuffer(const BufferDesc& description) = 0;
  virtual IRenderQueue* GetQueue(std::uint32_t index) = 0;
  virtual FenceHandle CreateFence(bool signaled = false) = 0;
  virtual void DestroyFence(FenceHandle) = 0;
  virtual void ResetFence(FenceHandle) = 0;
  virtual bool GetFenceStatus(FenceHandle) = 0;
  virtual bool WaitForFence(FenceHandle,
                            std::uint64_t timeout = UINT64_MAX) = 0;
  virtual SemaphoreHandle
  CreateSemaphore(SemaphoreType type = SemaphoreType::Binary,
                  std::uint64_t initialValue = 0) = 0;
  virtual void DestroySemaphore(SemaphoreHandle) = 0;
  virtual std::uint64_t GetSemaphoreCounterValue(SemaphoreHandle) = 0;
  virtual void SignalSemaphore(SemaphoreHandle, std::uint64_t value) = 0;
  virtual bool WaitSemaphore(SemaphoreHandle, std::uint64_t value,
                             std::uint64_t timeout = UINT64_MAX) = 0;
  virtual std::span<std::byte> MapMemory(BufferHandle) = 0;
  virtual void UnmapMemory(BufferHandle) = 0;
  virtual void FlushMappedMemoryRange(BufferHandle, std::uint64_t offset,
                                      std::uint64_t size) = 0;
  virtual void InvalidateMappedMemoryRange(BufferHandle, std::uint64_t offset,
                                           std::uint64_t size) = 0;
  virtual std::optional<BufferMetadata>
  GetBufferDesc(BufferHandle handle) const noexcept = 0;
  virtual bool IsValid(BufferHandle handle) const noexcept = 0;
  virtual bool IsValid(DescriptorSetLayoutHandle) const noexcept = 0;
  virtual bool IsValid(DescriptorSetHandle) const noexcept = 0;
  virtual bool IsValid(PipelineLayoutHandle) const noexcept = 0;
  virtual bool IsValid(SamplerHandle) const noexcept = 0;
  virtual ShaderModuleHandle
  CreateShaderModule(const ShaderModuleDesc& description) = 0;
  virtual ShaderModuleHandle CreateShaderModule(const Shader& shader)
  {
    const auto words = shader.GetSPIRV();
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(words.data()), words.size_bytes());
    return CreateShaderModule(
        ShaderModuleDesc{shader.GetStage(), bytes, shader.GetEntryPoint()});
  }
  virtual DescriptorSetLayoutHandle
  CreateDescriptorSetLayout(const DescriptorSetLayoutDesc&) = 0;
  virtual DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc&) = 0;
  virtual PipelineLayoutHandle
  CreatePipelineLayout(const PipelineLayoutDesc&) = 0;
  virtual SamplerHandle CreateSampler(const SamplerDesc&) = 0;
  // Commands record descriptor set handles and resolve their contents during
  // Submit. Updates before Submit are visible; submitted work is not
  // retroactively changed.
  virtual void UpdateDescriptorSet(DescriptorSetHandle,
                                   std::span<const DescriptorWrite>) = 0;
  virtual GraphicsPipelineHandle
  CreatePipeline(const GraphicsPipelineDesc& description) = 0;
  // Creates a material closest-hit pipeline library for later scene linking.
  // A backend advertising rayTracing=true must implement this API. Unsupported
  // backends fail explicitly; hardware support alone does not enable it.
  virtual RayTracingPipelineHandle
  CreateRayTracingPipeline(const RayTracingPipelineDesc&)
  {
    throw std::logic_error(
        "Ray-tracing pipelines are unsupported by this backend");
  }
  virtual void DestroyRayTracingPipeline(RayTracingPipelineHandle)
  {
    throw std::logic_error(
        "Ray-tracing pipelines are unsupported by this backend");
  }
  virtual std::unique_ptr<IRenderCommandList>
  CreateCommandList(std::uint32_t queueIndex) = 0;

  virtual void DestroyImage(ImageHandle handle) = 0;
  virtual void DestroyImageView(ImageViewHandle handle) = 0;
  virtual void DestroyBuffer(BufferHandle handle) = 0;
  virtual void DestroyPipeline(GraphicsPipelineHandle handle) = 0;
  virtual void DestroyModule(ShaderModuleHandle handle) = 0;
  virtual void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle) = 0;
  virtual void DestroyDescriptorSet(DescriptorSetHandle) = 0;
  virtual void DestroyPipelineLayout(PipelineLayoutHandle) = 0;
  virtual void DestroySampler(SamplerHandle) = 0;

  virtual RenderCapabilities GetCapabilities() const = 0;
  virtual void WaitIdle() = 0;

private:
  static std::uint64_t NextDeviceId() noexcept
  {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
  }
  std::uint64_t m_deviceId;
};

} // namespace Lighthouse
