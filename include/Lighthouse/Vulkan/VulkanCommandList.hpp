#pragma once

#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

#include <variant>
#include <vector>

namespace Lighthouse {

class VulkanCommandList final : public IRenderCommandList {
public:
  void BlitImage(ImageHandle src, ImageHandle dst, const BlitRegion &region,
                 ImageFilter filter) override {
    throw std::logic_error("Vulkan BlitImage is not implemented");
  }

  void BeginRendering(const RenderPassDesc &d) override {

  }
  void EndRendering() override {}
  void BindPipeline(GraphicsPipelineHandle h) override {

  }
  void BindDescriptorSet(std::uint32_t setIndex,
                         DescriptorSetHandle h) override {

  }
  void BindVertexBuffer(BufferHandle h, std::uint32_t stride) override {

  }
  void BindIndexBuffer(BufferHandle h) override {

  }
  void BindTexture(std::uint32_t slot, ImageHandle h) override {

  }
  void Draw(PrimitiveTopology t, std::uint32_t n, std::uint32_t first,
            std::uint32_t instances, std::uint32_t firstInstance) override {

  }
  void DrawIndexed(std::uint32_t n) override {

  }
  void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override {
 
  }


private:

};

} // namespace Lighthouse
