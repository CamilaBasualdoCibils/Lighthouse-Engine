#pragma once
#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>
#include <memory>
#include <vulkan/vulkan.hpp>

namespace Lighthouse
{
class VulkanRenderDevice;
// Records native Vulkan commands immediately; no replay/command vector.
class VulkanCommandList final : public IRenderCommandList
{
public:
  explicit VulkanCommandList(VulkanRenderDevice& device);
  ~VulkanCommandList() override;
  void Begin() override;
  void End() override;
  void Clear() override;
  void BeginRendering(const RenderPassDesc&) override;
  void EndRendering() override;
  void BindPipeline(GraphicsPipelineHandle) override;
  void BindDescriptorSet(std::uint32_t, DescriptorSetHandle) override;
  void BindVertexBuffer(BufferHandle, std::uint32_t) override;
  void BindIndexBuffer(BufferHandle) override;
  void BindTexture(std::uint32_t, ImageHandle) override;
  void Draw(PrimitiveTopology, std::uint32_t, std::uint32_t, std::uint32_t = 1,
            std::uint32_t = 0) override;
  void DrawIndexed(std::uint32_t) override;
  void DrawIndexedRange(std::uint32_t, std::uint32_t, std::int32_t) override;
  void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override;
  void BlitImage(ImageHandle, ImageHandle, const BlitRegion&,
                 ImageFilter) override;
  const vk::CommandBuffer& Native() const noexcept;

private:
  friend class VulkanRenderDevice;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace Lighthouse
