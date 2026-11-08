#include "Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp"
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
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
}
void WindowView::Begin(Renderer* renderer)
{

  const auto acquire =
      renderer->GetRenderDevice()->AcquireSwapchainImage(swapchain);
  m_acquiredImage = acquire;
  m_swapchainInfo =
      renderer->GetRenderDevice()->GetSwapchainInfo(swapchain).value_or(
          m_swapchainInfo);
}
void WindowView::End(Renderer* renderer)
{
  renderer->GetRenderDevice()->GetQueue(0)->Present(swapchain);
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
