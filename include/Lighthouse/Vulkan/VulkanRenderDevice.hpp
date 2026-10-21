#pragma once

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <memory>

namespace Lighthouse {

class Window;

// Minimal Vulkan 1.4 swapchain renderer using dynamic rendering.
class VulkanRenderDevice final : public IRenderDevice {
public:
    explicit VulkanRenderDevice(const DeviceConfig& config = {});
    ~VulkanRenderDevice() override;
    VulkanRenderDevice(const VulkanRenderDevice&) = delete;
    VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

    ImageHandle CreateImage(const ImageDesc&) override;
    ImageViewHandle CreateImageView(const ImageViewDesc&) override;
    BufferHandle CreateBuffer(const BufferDesc&) override;
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override;
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override;
    std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override;
    void Destroy(ImageHandle) override;
    void Destroy(ImageViewHandle) override;
    void Destroy(BufferHandle) override;
    void Destroy(GraphicsPipelineHandle) override;
    void Destroy(ShaderModuleHandle) override;
    RenderCapabilities GetCapabilities() const override;
    void CreateSwapchain(Window& window, const SwapchainConfig& config) override;
    void Submit(const IRenderCommandList&) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
