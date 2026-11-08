#pragma once
#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

namespace Lighthouse
{
enum class SemaphoreType
{
  Binary,
  Timeline
};
// Vulkan synchronization2 stage mask bits; AllCommands works on every queue.
enum class PipelineStage : std::uint64_t
{
  None = 0,
  TopOfPipe = 0x1,
  DrawIndirect = 0x2,
  VertexInput = 0x4,
  VertexShader = 0x8,
  FragmentShader = 0x80,
  EarlyFragmentTests = 0x100,
  LateFragmentTests = 0x200,
  ColorAttachmentOutput = 0x400,
  ComputeShader = 0x800,
  Transfer = 0x1000,
  BottomOfPipe = 0x2000,
  Host = 0x4000,
  AllGraphics = 0x8000,
  AllCommands = 0x10000
};
struct SemaphoreSubmitInfo
{
  SemaphoreHandle semaphore;
  std::uint64_t value{}; // zero for binary semaphores
  Flags<PipelineStage> stage{PipelineStage::AllCommands};
};
struct QueueSubmitInfo
{
  std::span<const IRenderCommandList* const> commandLists;
  std::span<const SemaphoreSubmitInfo> waits;
  std::span<const SemaphoreSubmitInfo> signals;
};
// Queue access is externally synchronized, as with vk::Queue. Command lists,
// resources and synchronization objects must outlive submitted GPU work.
class IRenderQueue
{
public:
  virtual ~IRenderQueue() = default;
  virtual Flags<RenderQueueCapability> GetCapabilities() const noexcept = 0;
  virtual void Submit(const QueueSubmitInfo&, FenceHandle fence = {}) = 0;
  void Submit(const IRenderCommandList& commands, FenceHandle fence = {})
  {
    const IRenderCommandList* list = &commands;
    Submit(QueueSubmitInfo{.commandLists = {&list, 1}}, fence);
  }
  virtual void Present(SwapchainHandle,
                       std::span<const SemaphoreHandle> waits = {}) = 0;
  virtual void WaitIdle() = 0;
};
} // namespace Lighthouse
