#include "Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp"
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

#include <algorithm>

namespace Lighthouse
{

void IRenderView::BuildGraph(ViewBuildContext&) {}

Window* WindowView::GetWindow() const noexcept
{
  return m_windows ? m_windows->GetWindow(window) : nullptr;
}

void WindowView::BuildGraph(ViewBuildContext& ctx)
{
  auto* targetWindow = GetWindow();
  if (!targetWindow)
    throw std::logic_error("Window view targets an invalid window");
  // if (!ctx.device) throw std::logic_error("Window view requires a render
  // device");
  if (!swapchain || !ctx.device.IsValid(swapchain))
  {
    if (!surface)
      surface = ctx.device.CreateSurface(*targetWindow);
    swapchain = ctx.device.CreateSwapchain(surface, presentation);
  }
  m_swapchainInfo =
      ctx.device.GetSwapchainInfo(swapchain).value_or(m_swapchainInfo);
  ImportedWindowView imported_image_view(this);
  m_acquiredImageAsRGResource = ctx.graph.Import(
      "WindowView Swapchain", ImportedWindowView::Desc{}, imported_image_view);
  m_logger->info("Imported WindowView Swapchain to RenderGraph");
  struct Data { RenderGraphResource image; };
  const auto& clear=ctx.graph.AddPass<Data>("Window background",[image=*m_acquiredImageAsRGResource](RenderGraph::Builder& builder,Data& data) {
    data.image=builder.Write(image);
    builder.SetSideEffect();
  },[this](const Data& data,FrameGraphPassResources& resources,RenderGraphExecutePassContext& execute) {
    const auto& image=resources.get<ImportedWindowView>(data.image);
    const auto size=GetWindow()->GetFramebufferSize();
    execute.commandList->BeginRendering({.colorAttachment=image.currentImageView,.clearColor=true,.width=size.width,.height=size.height});
    execute.commandList->EndRendering();
  });
  m_acquiredImageAsRGResource=clear.image;
}
void WindowView::Begin(RenderGraphComposer* renderer)
{

  const auto acquire =
      renderer->GetRenderDevice()->AcquireSwapchainImage(swapchain);
  m_acquiredImage = acquire;
  m_graphSubmitted=false;
  m_swapchainInfo =
      renderer->GetRenderDevice()->GetSwapchainInfo(swapchain).value_or(
          m_swapchainInfo);
}
void WindowView::End(RenderGraphComposer* renderer)
{
  const auto finished=m_acquiredImage->finished;
  renderer->GetRenderDevice()->GetQueue(0)->Present(swapchain,finished ? std::span(&finished,1) : std::span<const SemaphoreHandle>{});
  m_acquiredImage.reset();
}

void ExportView::BuildGraph(ViewBuildContext& ctx)
{
  if (!imageView)
    throw std::logic_error("Export view targets an invalid external image");
  m_exportImage = ctx.graph.Import<ImportedRenderGraphImage>(
      "export image", description, {imageView});
}

void RenderTextureView::BuildGraph(ViewBuildContext& ctx) {}

} // namespace Lighthouse
