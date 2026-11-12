#pragma once
#include "IRenderGraphAllocator.hpp"
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
namespace Lighthouse
{
// Releases arrive only after the graph submission fence. Pool allocations
// across executions; never alias allocations that are still in flight.
class SimpleRenderGraphAllocator : public IRenderGraphAllocator
{
public:
  explicit SimpleRenderGraphAllocator(IRenderDevice& device):m_device(device) {}
  ~SimpleRenderGraphAllocator()
  {
    for(auto& image:m_images) { m_device.DestroyImageView(image.allocation.view); m_device.DestroyImage(image.allocation.image); }
    for(auto& buffer:m_buffers) m_device.DestroyBuffer(buffer.allocation.buffer);
  }
  // Rebuilds may change every extent. Release retired allocations once old
  // graph callbacks/views are destroyed instead of retaining every resize.
  void TrimUnused()
  {
    std::erase_if(m_images,[&](const Image& image) {
      if(!image.available) return false;
      m_device.DestroyImageView(image.allocation.view);
      m_device.DestroyImage(image.allocation.image);
      return true;
    });
    std::erase_if(m_buffers,[&](const Buffer& buffer) {
      if(!buffer.available) return false;
      m_device.DestroyBuffer(buffer.allocation.buffer);
      return true;
    });
  }
  RenderGraphImageAllocation AllocateImage(const RenderGraphImageDesc& desc) override
  {
    for(auto& image:m_images) {
      const auto& d=image.desc;
      if(image.available && d.width==desc.width && d.height==desc.height && d.format==desc.format && *d.usage==*desc.usage) {
        image.available=false; return image.allocation;
      }
    }
    const auto image=m_device.CreateImage({.width=desc.width,.height=desc.height,.format=desc.format,.usage=desc.usage});
    ImageViewHandle view;
    try { view=m_device.CreateImageView({.image=image,.format=desc.format}); }
    catch (...) { m_device.DestroyImage(image); throw; }
    ImageSubresourceRange range;
    if(desc.format==ImageFormat::Depth16 || desc.format==ImageFormat::Depth32) range.aspectMask=ImageAspect::Depth;
    else if(desc.format==ImageFormat::Depth24Stencil8) range.aspectMask={ImageAspect::Depth,ImageAspect::Stencil};
    RenderGraphImageAllocation result{image,view,range};
    m_images.push_back({desc,result,false}); return result;
  }
  void FreeImage(ImageViewHandle view) override
  { for(auto& image:m_images) if(image.allocation.view==view) { image.available=true; return; } }
  RenderGraphBufferAllocation AllocateBuffer(const RenderGraphBufferDesc& desc) override
  {
    for(auto& buffer:m_buffers)
      if(buffer.available && buffer.desc.size==desc.size && *buffer.desc.usage==*desc.usage) {
        buffer.available=false; return buffer.allocation;
      }
    RenderGraphBufferAllocation result{m_device.CreateBuffer({.size=desc.size,.usage=desc.usage})};
    m_buffers.push_back({desc,result,false}); return result;
  }
  void FreeBuffer(BufferHandle handle) override
  { for(auto& buffer:m_buffers) if(buffer.allocation.buffer==handle) { buffer.available=true; return; } }
private:
  struct Image { RenderGraphImageDesc desc; RenderGraphImageAllocation allocation; bool available; };
  struct Buffer { RenderGraphBufferDesc desc; RenderGraphBufferAllocation allocation; bool available; };
  IRenderDevice& m_device;
  std::vector<Image> m_images;
  std::vector<Buffer> m_buffers;
};
}
