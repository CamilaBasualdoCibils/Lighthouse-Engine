#pragma once

#include "IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Rendering/IRenderDevice.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
namespace Lighthouse
{
class SimpleRenderGraphAllocator : public IRenderGraphAllocator
{
public:
  SimpleRenderGraphAllocator(IRenderDevice& device) : m_device(device) {}
  ~SimpleRenderGraphAllocator()
  {
    for (auto& [view, image] : m_imageViews)
    {
      m_device.DestroyImageView(view);
      m_device.DestroyImage(image);
    }
  }

  // Implement the IRenderGraphAllocator interface here

public:
  RenderGraphImageAllocation
  AllocateImage(const RenderGraphImageDesc& desc) override
  {
    ImageDesc imageDesc{};
    imageDesc.width = desc.width;
    imageDesc.height = desc.height;
    imageDesc.format = desc.format;
    imageDesc.usage = desc.usage;
    ImageHandle image = m_device.CreateImage(imageDesc);
    ImageViewDesc viewDesc{};
    viewDesc.image = image;
    viewDesc.format = imageDesc.format;
    viewDesc.type = imageDesc.type;
    ImageViewHandle view = m_device.CreateImageView(viewDesc);
    m_imageViews[view] = image;
    ImageSubresourceRange subresource{.aspectMask = {ImageAspect::Color},
                                      .baseMipLevel = 0,
                                      .mipLevelCount = 1,
                                      .baseArrayLayer = 0,
                                      .layerCount = 1};
    if (desc.format == ImageFormat::Depth16 ||
        desc.format == ImageFormat::Depth32)
      subresource.aspectMask = ImageAspect::Depth;
    else if (desc.format == ImageFormat::Depth24Stencil8)
      subresource.aspectMask = {ImageAspect::Depth, ImageAspect::Stencil};
    RenderGraphImageAllocation allocation{image, view, subresource};

    return allocation;
  }

  void FreeImage(ImageViewHandle view) override
  {
    // TODO: Implement this pure virtual method.
    auto it = m_imageViews.find(view);
    if (it != m_imageViews.end())
    {
      m_device.DestroyImageView(view);
      m_device.DestroyImage(it->second);
      m_imageViews.erase(it);
    }
  }
  RenderGraphBufferAllocation
  AllocateBuffer(const RenderGraphBufferDesc& rgdesc) override
  {
    BufferDesc desc{};
    desc.size = rgdesc.size;
    desc.usage = rgdesc.usage;
    BufferHandle buffer = m_device.CreateBuffer(desc);
    RenderGraphBufferAllocation allocation{buffer};
    m_logger->info("Allocated buffer ");
    return allocation;
  }

  void FreeBuffer(BufferHandle buffer) override
  {
    // TODO: Implement this pure virtual method.
    m_device.DestroyBuffer(buffer);
    m_logger->info("Freed buffer ");
  }

private:
  IRenderDevice& m_device;

  std::unordered_map<ImageViewHandle, ImageHandle> m_imageViews;
  std::shared_ptr<spdlog::logger> m_logger =
      spdlog::get("RenderGraphSimpleAllocator")
          ? spdlog::get("RenderGraphSimpleAllocator")
          : spdlog::stdout_color_mt("RenderGraphSimpleAllocator");
};
} // namespace Lighthouse
