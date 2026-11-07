#pragma once

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

  // Native window requirement used by Renderer when creating a window view.
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

  virtual SwapchainHandle CreateSwapchain(Window&, const SwapchainConfig&) = 0;
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
  // Synchronous host upload: completes prior device work, copies the bytes,
  // and makes them visible to subsequent submissions. It does not record a
  // command and must not race other threads submitting work on this device.
  virtual void UpdateBuffer(BufferHandle, std::uint64_t,
                            std::span<const std::byte>)
  {
    throw std::logic_error("Buffer updates are unsupported by this backend");
  }
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
    throw std::logic_error("Ray-tracing pipelines are unsupported by this backend");
  }
  virtual void DestroyRayTracingPipeline(RayTracingPipelineHandle)
  {
    throw std::logic_error("Ray-tracing pipelines are unsupported by this backend");
  }
  virtual std::unique_ptr<IRenderCommandList>
  CreateCommandList(QueueType type) = 0;

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
  // Execute commands against their declared attachments. One command list may
  // write multiple acquired swapchains; each becomes independently presentable.
  virtual void Submit(const IRenderCommandList& commandList) = 0;
  // Display this swapchain's acquired image after its submitted work completes.
  virtual void Present(SwapchainHandle swapchain) = 0;
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
