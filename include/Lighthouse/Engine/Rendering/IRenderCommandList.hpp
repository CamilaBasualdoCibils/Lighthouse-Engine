#pragma once

#include "Lighthouse/Engine/Core/Types.hpp"
#include <array>
#include <cstdint>

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>
#include <stdexcept>

namespace Lighthouse
{

struct DrawIndexedIndirectCommand
{
  std::uint32_t indexCount, instanceCount, firstIndex;
  std::int32_t baseVertex;
  std::uint32_t firstInstance;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == 20);

class IRenderCommandList
{
public:
  virtual ~IRenderCommandList() = default;

  virtual void PushDebugGroup(std::string_view name,
                              std::array<float, 4> color = {1, 1, 1, 1})
  {
  }
  virtual void PopDebugGroup() {}
  virtual void InsertDebugMarker(std::string_view name,
                                 std::array<float, 4> color = {1, 1, 1, 1})
  {
  }
  virtual void Begin() = 0;
  virtual void End() = 0;
  virtual void MemoryBarrier(const MemoryBarrierDesc&)
  {
    throw std::logic_error("Memory barriers unsupported by this backend");
  }
  virtual void ImageBarrier(const ImageBarrierDesc&)
  {
    throw std::logic_error("Image barriers unsupported by this backend");
  }
  virtual void BeginRendering(const RenderPassDesc& description) = 0;
  virtual void EndRendering() = 0;
  // Legacy OpenGL state override. Native pipeline backends use depthStencil
  // in GraphicsPipelineDesc and SetStencilReference instead.
  virtual void SetStencil(StencilMode mode, std::uint32_t reference)
  {
    if (mode != StencilMode::Disabled)
      throw std::logic_error("Stencil state is unsupported by this backend");
  }
  virtual void SetStencilReference(std::uint32_t)
  {
    throw std::logic_error("Dynamic stencil reference unsupported by this backend");
  }
  virtual void BindPipeline(GraphicsPipelineHandle pipeline) = 0;
  // Descriptor contents are resolved when the recorded command list is
  // submitted.
  virtual void BindDescriptorSet(std::uint32_t setIndex,
                                 DescriptorSetHandle set) = 0;
  virtual void SetDrawIndex(std::uint32_t)
  {
    throw std::logic_error("Draw-index constants unsupported by this backend");
  }
  virtual void BindVertexBuffer(BufferHandle buffer, std::uint32_t stride) = 0;
  virtual void BindIndexBuffer(BufferHandle buffer) = 0;
  virtual void BindTexture(std::uint32_t slot, ImageHandle image) = 0;
  virtual void Draw(PrimitiveTopology topology, std::uint32_t vertexCount,
                    std::uint32_t firstVertex, std::uint32_t instanceCount = 1,
                    std::uint32_t firstInstance = 0) = 0;
  virtual void DrawIndexed(std::uint32_t indexCount) = 0;
  virtual void DrawIndexedRange(std::uint32_t indexCount,
                                std::uint32_t firstIndex,
                                std::int32_t baseVertex)
  {
    if (firstIndex || baseVertex)
      throw std::logic_error(
          "Indexed draw ranges are unsupported by this backend");
    DrawIndexed(indexCount);
  }
  // Buffer contains tightly packed DrawIndexedIndirectCommand records.
  virtual void MultiDrawIndexedIndirect(BufferHandle, std::uint32_t)
  {
    throw std::logic_error("Indexed multidraw unsupported by this backend");
  }
  virtual void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;

  // Copies distinct buffers; offsets/size are nonzero-size, 4-byte aligned.
  // The caller declares graph dependencies.
  virtual void CopyBuffer(BufferHandle, BufferHandle, std::uint64_t,
                          std::uint64_t, std::uint64_t)
  {
    throw std::logic_error("Buffer copy unsupported");
  }

  // Caller declares graph dependencies; generated levels finish ready for sampling.
  virtual void GenMipMaps(const GenMipMapsDesc&)
  {
    throw std::logic_error("Mipmap generation unsupported by this backend");
  }
  // Images must already be in TransferSource / TransferDestination layouts.
  virtual void BlitImage(ImageHandle src, ImageHandle dst,
                         const BlitRegion& region, ImageFilter filter) = 0;
  virtual void Clear() = 0;
};

} // namespace Lighthouse
