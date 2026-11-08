#pragma once
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
class NullSynchronizationDevice : public Lighthouse::IRenderDevice
{
  class Queue final : public Lighthouse::IRenderQueue
  {
    Flags<Lighthouse::RenderQueueCapability>
    GetCapabilities() const noexcept override
    {
      return {};
    }
    void Submit(const Lighthouse::QueueSubmitInfo&,
                Lighthouse::FenceHandle) override
    {
    }
    void Present(Lighthouse::SwapchainHandle,
                 std::span<const Lighthouse::SemaphoreHandle>) override
    {
    }
    void WaitIdle() override {}
  } queue;
  unsigned nextFence = 1;

public:
  Lighthouse::SurfaceHandle CreateSurface(Lighthouse::Window&) override
  {
    return {1, 1, DeviceId()};
  }
  void DestroySurface(Lighthouse::SurfaceHandle) override {}
  Lighthouse::IRenderQueue* GetQueue(std::uint32_t index) override
  {
    return index == 0 ? &queue : nullptr;
  }
  Lighthouse::FenceHandle CreateFence(bool = false) override
  {
    return {nextFence++, 1, DeviceId()};
  }
  void DestroyFence(Lighthouse::FenceHandle) override {}
  void ResetFence(Lighthouse::FenceHandle) override {}
  bool GetFenceStatus(Lighthouse::FenceHandle) override
  {
    return true;
  }
  bool WaitForFence(Lighthouse::FenceHandle, std::uint64_t) override
  {
    return true;
  }
  Lighthouse::SemaphoreHandle CreateSemaphore(Lighthouse::SemaphoreType,
                                              std::uint64_t) override
  {
    throw std::logic_error("Unused test semaphore");
  }
  void DestroySemaphore(Lighthouse::SemaphoreHandle) override {}
  std::uint64_t GetSemaphoreCounterValue(Lighthouse::SemaphoreHandle) override
  {
    return 0;
  }
  void SignalSemaphore(Lighthouse::SemaphoreHandle, std::uint64_t) override {}
  bool WaitSemaphore(Lighthouse::SemaphoreHandle, std::uint64_t,
                     std::uint64_t) override
  {
    return true;
  }
  std::span<std::byte> MapMemory(Lighthouse::BufferHandle) override
  {
    throw std::logic_error("Unused test mapping");
  }
  void UnmapMemory(Lighthouse::BufferHandle) override {}
  void FlushMappedMemoryRange(Lighthouse::BufferHandle, std::uint64_t,
                              std::uint64_t) override
  {
  }
  void InvalidateMappedMemoryRange(Lighthouse::BufferHandle, std::uint64_t,
                                   std::uint64_t) override
  {
  }
};
