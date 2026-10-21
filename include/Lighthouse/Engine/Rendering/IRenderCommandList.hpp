#pragma once

#include <cstdint>

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>

namespace Lighthouse {

class IRenderCommandList {
public:
    virtual ~IRenderCommandList() = default;

    virtual void BeginRendering(const RenderPassDesc& description) = 0;
    virtual void EndRendering() = 0;
    virtual void BindPipeline(GraphicsPipelineHandle pipeline) = 0;
    virtual void BindVertexBuffer(BufferHandle buffer, std::uint32_t stride) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer) = 0;
    virtual void BindTexture(std::uint32_t slot, ImageHandle image) = 0;
    virtual void Draw(PrimitiveTopology topology, std::uint32_t vertexCount,
                      std::uint32_t firstVertex, std::uint32_t instanceCount = 1,
                      std::uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(std::uint32_t indexCount) = 0;
    virtual void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
};

} // namespace Lighthouse
