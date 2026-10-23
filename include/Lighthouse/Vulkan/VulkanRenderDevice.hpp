#pragma once

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#endif

#include <memory>

namespace Lighthouse {

class Window;

// Minimal Vulkan 1.4 swapchain renderer using dynamic rendering.
class VulkanRenderDevice final : public IRenderDevice
#ifdef LIGHTHOUSE_IMGUI
    , public IImGuiDevice
#endif
{
public:
    using IRenderDevice::CreateShaderModule;
    explicit VulkanRenderDevice(const DeviceConfig& config = {});
    ~VulkanRenderDevice() override;
    VulkanRenderDevice(const VulkanRenderDevice&) = delete;
    VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

    ImageHandle CreateImage(const ImageDesc&) override;
    ImageViewHandle CreateImageView(const ImageViewDesc&) override;
    BufferHandle CreateBuffer(const BufferDesc&) override;
    std::optional<BufferMetadata> GetBufferDesc(BufferHandle) const noexcept override;
    bool IsValid(BufferHandle) const noexcept override;
    bool IsValid(DescriptorSetLayoutHandle) const noexcept override;
    bool IsValid(DescriptorSetHandle) const noexcept override;
    bool IsValid(PipelineLayoutHandle) const noexcept override;
    bool IsValid(SamplerHandle) const noexcept override;
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override;
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc&) override;
    DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc&) override;
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc&) override;
    SamplerHandle CreateSampler(const SamplerDesc&) override;
    void UpdateDescriptorSet(DescriptorSetHandle, std::span<const DescriptorWrite>) override;
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override;
    std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override;
    void Destroy(ImageHandle) override;
    void Destroy(ImageViewHandle) override;
    void Destroy(BufferHandle) override;
    void Destroy(GraphicsPipelineHandle) override;
    void Destroy(ShaderModuleHandle) override;
    void Destroy(DescriptorSetLayoutHandle) override;
    void Destroy(DescriptorSetHandle) override;
    void Destroy(PipelineLayoutHandle) override;
    void Destroy(SamplerHandle) override;
    RenderCapabilities GetCapabilities() const override;
    SwapchainHandle CreateSwapchain(Window& window, const SwapchainConfig& config) override;
    ImageViewHandle AcquireSwapchainImage(SwapchainHandle) override;
    bool DestroySwapchain(SwapchainHandle) override;
    bool IsValid(SwapchainHandle) const noexcept override;
    void Submit(const IRenderCommandList&) override;
    void Present(SwapchainHandle swapchain) override;
#ifdef LIGHTHOUSE_IMGUI
    ImGuiPlatformBackend PlatformBackend() const noexcept override { return ImGuiPlatformBackend::Vulkan; }
    void InitializeImGui(SwapchainHandle swapchain) override;
    void ShutdownImGui() noexcept override;
    void NewImGuiFrame() override;
    void RenderImGui(SwapchainHandle swapchain, ImageViewHandle image, ImDrawData* drawData) override;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
