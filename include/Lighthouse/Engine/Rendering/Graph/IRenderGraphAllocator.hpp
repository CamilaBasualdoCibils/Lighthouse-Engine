#pragma once

#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include <cstdint>
namespace Lighthouse {
struct RenderGraphImageAllocation {
  ImageHandle image;
  ImageViewHandle view;
  ImageSubresourceRange subresource;
};
struct RenderGraphImageDesc {
  uint32_t width;
  uint32_t height;
  ImageFormat format;
  Flags<ImageUsage> usage{ImageUsage::Sampled, ImageUsage::ColorAttachment};
};
struct RenderGraphBufferAllocation
{
  BufferHandle buffer;
};
struct RenderGraphBufferDesc
{
  uint64_t size;
  Flags<BufferUsage> usage{};
};

class IRenderGraphAllocator {
public:
  virtual ~IRenderGraphAllocator() = default;

  virtual RenderGraphImageAllocation
  AllocateImage(const RenderGraphImageDesc &desc) = 0;
  virtual void FreeImage(ImageViewHandle image) = 0;
  virtual RenderGraphBufferAllocation
  AllocateBuffer(const RenderGraphBufferDesc &desc) = 0;
  virtual void FreeBuffer(BufferHandle buffer) = 0;



  /* virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
  virtual void DestroyBuffer(BufferHandle buffer) = 0; */
};
} // namespace Lighthouse