#pragma once
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
inline void SubmitAndWait(Lighthouse::IRenderDevice& device,
                          const Lighthouse::IRenderCommandList& commands)
{
  const auto fence = device.CreateFence();
  try
  {
    device.GetQueue(0)->Submit(commands, fence);
    device.WaitForFence(fence);
  }
  catch (...)
  {
    device.DestroyFence(fence);
    throw;
  }
  device.DestroyFence(fence);
}

inline void SubmitPresentation(Lighthouse::IRenderDevice& device,const Lighthouse::IRenderCommandList& commands,
                               std::initializer_list<Lighthouse::AcquiredSwapchainImage> images)
{
  using namespace Lighthouse;
  std::vector<SemaphoreSubmitInfo> waits,signals;
  for(const auto& image:images) {
    if(image.available) waits.push_back({image.available,0,PipelineStage::AllCommands});
    if(image.finished) signals.push_back({image.finished,0,PipelineStage::AllCommands});
  }
  const auto fence=device.CreateFence();
  const IRenderCommandList* list=&commands;
  device.GetQueue(0)->Submit({.commandLists={&list,1},.waits=waits,.signals=signals},fence);
  device.WaitForFence(fence);device.DestroyFence(fence);
}
inline void SubmitAndPresent(Lighthouse::IRenderDevice& device,const Lighthouse::IRenderCommandList& commands,
                             Lighthouse::SwapchainHandle swap,const Lighthouse::AcquiredSwapchainImage& image)
{
  SubmitPresentation(device,commands,{image});
  device.GetQueue(0)->Present(swap,image.finished ? std::span(&image.finished,1) : std::span<const Lighthouse::SemaphoreHandle>{});
}
