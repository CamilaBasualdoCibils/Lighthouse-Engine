#pragma once
#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

namespace Lighthouse
{
enum class SemaphoreType
{
  Binary,
  Timeline
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
