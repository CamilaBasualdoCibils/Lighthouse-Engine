#pragma once

#include <memory>
#include <optional>
#include <atomic>
#include <stdexcept>

#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

namespace Lighthouse {

class Window;

class IRenderDevice {
public:
    IRenderDevice() : m_deviceId(NextDeviceId()) {}
    virtual ~IRenderDevice() = default;
    IRenderDevice(const IRenderDevice&) = delete;
    IRenderDevice& operator=(const IRenderDevice&) = delete;

    std::uint64_t DeviceId() const noexcept { return m_deviceId; }

    virtual SwapchainHandle CreateSwapchain(Window&, const SwapchainConfig&) = 0;
    // Returns an attachment handle for the currently acquired swapchain image.
    virtual ImageViewHandle AcquireSwapchainImage(SwapchainHandle) = 0;
    virtual bool DestroySwapchain(SwapchainHandle) = 0;
    virtual bool IsValid(SwapchainHandle) const noexcept = 0;

    virtual ImageHandle CreateImage(const ImageDesc& description) = 0;
    virtual ImageViewHandle CreateImageView(const ImageViewDesc& description) = 0;
    virtual BufferHandle CreateBuffer(const BufferDesc& description) = 0;
    virtual std::optional<BufferMetadata> GetBufferDesc(BufferHandle handle) const noexcept = 0;
    virtual bool IsValid(BufferHandle handle) const noexcept = 0;
    virtual ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc& description) = 0;
    virtual GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc& description) = 0;
    virtual std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType type) = 0;

    virtual void Destroy(ImageHandle handle) = 0;
    virtual void Destroy(ImageViewHandle handle) = 0;
    virtual void Destroy(BufferHandle handle) = 0;
    virtual void Destroy(GraphicsPipelineHandle handle) = 0;
    virtual void Destroy(ShaderModuleHandle handle) = 0;

    virtual RenderCapabilities GetCapabilities() const = 0;
    // Execute commands against their declared attachments.
    virtual void Submit(const IRenderCommandList& commandList) = 0;
    // Display the acquired swapchain image after its submitted work completes.
    virtual void Present(SwapchainHandle swapchain) = 0;

private:
    static std::uint64_t NextDeviceId() noexcept {
        static std::atomic<std::uint64_t> next{1};
        return next.fetch_add(1, std::memory_order_relaxed);
    }
    std::uint64_t m_deviceId;
};

} // namespace Lighthouse
