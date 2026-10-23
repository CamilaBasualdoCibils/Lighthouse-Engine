#pragma once

#include <memory>
#include <cstdint>

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#endif

namespace Lighthouse {

namespace Detail { struct OpenGLDeviceGroup; }
class OpenGLCommandList;

// OpenGL 4.5 device backed by an EGL context and window surface.
class OpenGLRenderDevice final : public IRenderDevice
#ifdef LIGHTHOUSE_IMGUI
    , public IImGuiDevice
#endif
{
public:
    using IRenderDevice::CreateShaderModule;
    explicit OpenGLRenderDevice(std::shared_ptr<Detail::OpenGLDeviceGroup> group,
                                const DeviceConfig& config = {});
    ~OpenGLRenderDevice() override;
    OpenGLRenderDevice(const OpenGLRenderDevice&) = delete;
    OpenGLRenderDevice& operator=(const OpenGLRenderDevice&) = delete;

    ImageHandle CreateImage(const ImageDesc& description) override;
    ImageViewHandle CreateImageView(const ImageViewDesc& description) override;
    BufferHandle CreateBuffer(const BufferDesc& description) override;
    std::optional<BufferMetadata> GetBufferDesc(BufferHandle handle) const noexcept override;
    bool IsValid(BufferHandle handle) const noexcept override;
    bool IsValid(DescriptorSetLayoutHandle handle) const noexcept override;
    bool IsValid(DescriptorSetHandle handle) const noexcept override;
    bool IsValid(PipelineLayoutHandle handle) const noexcept override;
    bool IsValid(SamplerHandle handle) const noexcept override;
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc& description) override;
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc& description) override;
    DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc& description) override;
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc& description) override;
    SamplerHandle CreateSampler(const SamplerDesc& description) override;
    void UpdateDescriptorSet(DescriptorSetHandle set, std::span<const DescriptorWrite> writes) override;
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc& description) override;
    std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType type) override;
    void Destroy(ImageHandle handle) override;
    void Destroy(ImageViewHandle handle) override;
    void Destroy(BufferHandle handle) override;
    void Destroy(GraphicsPipelineHandle handle) override;
    void Destroy(ShaderModuleHandle handle) override;
    void Destroy(DescriptorSetLayoutHandle handle) override;
    void Destroy(DescriptorSetHandle handle) override;
    void Destroy(PipelineLayoutHandle handle) override;
    void Destroy(SamplerHandle handle) override;
    RenderCapabilities GetCapabilities() const override;
    SwapchainHandle CreateSwapchain(Window& window, const SwapchainConfig& config) override;
    ImageViewHandle AcquireSwapchainImage(SwapchainHandle) override;
    bool DestroySwapchain(SwapchainHandle swapchain) override;
    bool IsValid(SwapchainHandle swapchain) const noexcept override;
    void Submit(const IRenderCommandList& commandList) override;
    void Present(SwapchainHandle swapchain) override;
    ImageViewHandle AcquiredImageView() const noexcept;
#ifdef LIGHTHOUSE_IMGUI
    void InitializeImGui(SwapchainHandle swapchain) override;
    void ShutdownImGui() noexcept override;
    void NewImGuiFrame() override;
    void RenderImGui(SwapchainHandle swapchain, ImageViewHandle image, ImDrawData* drawData) override;
#endif

private:
    friend class OpenGLCommandList;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
