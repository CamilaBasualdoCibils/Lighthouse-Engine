#pragma once

#include "Lighthouse/Engine/Core/Types.hpp"
#include "vulkan/vulkan.hpp"
#include <cstdint>

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>
#include <vulkan/vulkan_core.h>

namespace Lighthouse {

class IRenderCommandList {
public:
    virtual ~IRenderCommandList() = default;

    virtual void BeginRendering(const RenderPassDesc& description) = 0;
    virtual void EndRendering() = 0;
    virtual void BindPipeline(GraphicsPipelineHandle pipeline) = 0;
    // Descriptor contents are resolved when the recorded command list is submitted.
    virtual void BindDescriptorSet(std::uint32_t setIndex, DescriptorSetHandle set) = 0;
    virtual void BindVertexBuffer(BufferHandle buffer, std::uint32_t stride) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer) = 0;
    virtual void BindTexture(std::uint32_t slot, ImageHandle image) = 0;
    virtual void Draw(PrimitiveTopology topology, std::uint32_t vertexCount,
                      std::uint32_t firstVertex, std::uint32_t instanceCount = 1,
                      std::uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(std::uint32_t indexCount) = 0;
    virtual void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;


   virtual void BlitImage(ImageHandle src, ImageHandle dst, const BlitRegion& region, ImageFilter filter) = 0;
   virtual void Clear() = 0;
};

} // namespace Lighthouse
