#include "Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp"
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

#include <algorithm>

namespace Lighthouse {

IRenderView::IRenderView()  {
  static std::atomic<std::uint64_t> next{1};
  m_ownerId = next.fetch_add(1, std::memory_order_relaxed);
}

IRenderView::Slot *
IRenderView::FindSlot(RenderTechniqueHandle handle) noexcept {
  if (!handle || handle.owner != m_ownerId ||
      handle.index >= m_techniques.size())
    return nullptr;
  auto &slot = m_techniques[handle.index];
  return slot.value && slot.generation == handle.generation ? &slot : nullptr;
}
const IRenderView::Slot *
IRenderView::FindSlot(RenderTechniqueHandle handle) const noexcept {
  return const_cast<IRenderView *>(this)->FindSlot(handle);
}
IRenderTechnique *
IRenderView::GetTechnique(RenderTechniqueHandle handle) noexcept {
  auto *slot = FindSlot(handle);
  return slot ? slot->value.get() : nullptr;
}
const IRenderTechnique *
IRenderView::GetTechnique(RenderTechniqueHandle handle) const noexcept {
  auto *slot = FindSlot(handle);
  return slot ? slot->value.get() : nullptr;
}
bool IRenderView::RemoveTechnique(RenderTechniqueHandle handle) {
  auto *slot = FindSlot(handle);
  if (!slot)
    return false;
  slot->value.reset();
  if (++slot->generation == 0)
    ++slot->generation;
  return true;
}

std::vector<RenderTechniqueHandle> IRenderView::Techniques() const {
  std::vector<RenderTechniqueHandle> result;
  for (std::uint32_t i = 0; i < m_techniques.size(); ++i)
    if (m_techniques[i].value)
      result.push_back({i, m_techniques[i].generation, m_ownerId});
  return result;
}
void IRenderView::BuildGraph(ViewBuildContext &ctx) { BuildTechniques(ctx); }

void IRenderView::BuildTechniques(ViewBuildContext &ctx) {
  for (auto handle : Techniques()) {
    TechniqueBuildContext techniqueContext(ctx.graph, *this, ctx.device);
    GetTechnique(handle)->BuildGraph(techniqueContext);
  }
}

Window *WindowView::GetWindow() const noexcept {
  return m_windows ? m_windows->GetWindow(window) : nullptr;
}

void WindowView::BuildGraph(ViewBuildContext &ctx) {
  auto *targetWindow = GetWindow();
  if (!targetWindow)
    throw std::logic_error("Window view targets an invalid window");
  // if (!ctx.device) throw std::logic_error("Window view requires a render
  // device");
  if (!swapchain || !ctx.device.IsValid(swapchain))
    swapchain = ctx.device.CreateSwapchain(*targetWindow, presentation);
  ImportedWindowView imported_image_view(this);
  m_acquiredImageAsRGResource = ctx.graph.Import(
      "WindowView Swapchain", ImportedWindowView::Desc{}, imported_image_view);
  m_logger->info("Imported WindowView Swapchain to RenderGraph");
  BuildTechniques(ctx);
}
void WindowView::Begin(Renderer *renderer) {
    m_logger->info("Acquiring swapchain image");
  const auto acquire =
      renderer->GetRenderDevice()->AcquireSwapchainImage(swapchain);
  m_acquiredImage = acquire;
}
void WindowView::End(Renderer *renderer) {
    m_logger->info("Presenting swapchain image");
  renderer->GetRenderDevice()->Present(swapchain);
  m_acquiredImage.reset();
}

void ExportView::BuildGraph(ViewBuildContext &ctx) {
  if (!imageView)
    throw std::logic_error("Export view targets an invalid external image");
  m_exportImage = ctx.graph.Import<ImportedRenderGraphImage>(
      "export image", description, {imageView});
  BuildTechniques(ctx);
}

void RenderTextureView::BuildGraph(ViewBuildContext &ctx) {
  BuildTechniques(ctx);
}

} // namespace Lighthouse
