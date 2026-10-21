#pragma once

#include <memory>
#include <stdexcept>

#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

namespace Lighthouse {

class Window;

class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    virtual void CreateSwapchain(Window&, const SwapchainConfig&) {
        throw std::logic_error("This render device does not support swapchains");
    }

    virtual ImageHandle CreateImage(const ImageDesc& description) = 0;
    virtual ImageViewHandle CreateImageView(const ImageViewDesc& description) = 0;
    virtual BufferHandle CreateBuffer(const BufferDesc& description) = 0;
    virtual ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc& description) = 0;
    virtual GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc& description) = 0;
    virtual std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType type) = 0;

    virtual void Destroy(ImageHandle handle) = 0;
    virtual void Destroy(ImageViewHandle handle) = 0;
    virtual void Destroy(BufferHandle handle) = 0;
    virtual void Destroy(GraphicsPipelineHandle handle) = 0;
    virtual void Destroy(ShaderModuleHandle handle) = 0;

    virtual RenderCapabilities GetCapabilities() const = 0;
    virtual void Submit(const IRenderCommandList& commandList) = 0;
};

} // namespace Lighthouse
