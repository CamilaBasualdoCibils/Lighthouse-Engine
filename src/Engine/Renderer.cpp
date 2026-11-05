#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>

#include "RenderMeshManager.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace Lighthouse {

Renderer::Renderer(std::unique_ptr<IRenderDevice> device,WindowManager *windows)
    : m_windows(windows),
      m_device(std::move(device)),
      m_renderGraphAllocator(*m_device.get()),
      m_renderMeshManager(std::make_unique<RenderMeshManager>()) {
  static std::atomic<std::uint64_t> nextOwner{1};
  m_ownerId = nextOwner.fetch_add(1, std::memory_order_relaxed);
}
Renderer::~Renderer() { }

RenderMeshHandle Renderer::CreateRenderMesh(const RenderMesh &description) {
  return m_renderMeshManager->Create(description, m_device.get());
}
bool Renderer::DestroyRenderMesh(RenderMeshHandle mesh) {
  return m_renderMeshManager->Destroy(mesh);
}
const RenderMesh *
Renderer::GetRenderMesh(RenderMeshHandle mesh) const noexcept {
  return m_renderMeshManager->GetDescription(mesh, m_device.get());
}
bool Renderer::IsValid(RenderMeshHandle mesh) const noexcept {
  return m_renderMeshManager->IsValid(mesh, m_device.get());
}
RenderMeshUploadStatus
Renderer::GetRenderMeshUploadStatus(RenderMeshHandle mesh) const noexcept {
  return m_renderMeshManager->GetUploadStatus(mesh, m_device.get());
}

IRenderView *Renderer::GetView(RenderViewHandle handle) noexcept {
  if (!handle || handle.owner != m_ownerId || handle.index >= m_views.size())
    return nullptr;
  auto &slot = m_views[handle.index];
  return slot.generation == handle.generation ? slot.value.get() : nullptr;
}
const IRenderView *Renderer::GetView(RenderViewHandle handle) const noexcept {
  return const_cast<Renderer *>(this)->GetView(handle);
}
bool Renderer::DestroyView(RenderViewHandle handle) {
  auto *view = GetView(handle);
  if (!view)
    return false;
  if (auto *window = dynamic_cast<WindowView *>(view);
      window && window->swapchain && m_device)
    m_device->DestroySwapchain(window->swapchain);
  auto &slot = m_views[handle.index];
  slot.value.reset();
  if (++slot.generation == 0)
    ++slot.generation;
  return true;
}

void Renderer::Compile() {
  RenderGraph graph;
  std::vector<unsigned char> state(m_views.size());
  std::function<void(RenderViewHandle)> build = [&](RenderViewHandle handle) {
    auto *view = GetView(handle);
    if (!view)
      throw std::logic_error("View dependency is missing or invalidated");
    if (!view->HasValidReferences())
      throw std::logic_error("View scene or camera is invalid");
    if (state[handle.index] == 1)
      throw std::logic_error("Cyclic view dependency");
    if (state[handle.index] == 2)
      return;
    state[handle.index] = 1;
    for (auto dependency : view->ViewDependencies())
      build(dependency);

    ViewBuildContext context{graph, *m_device.get()};
    view->BuildGraph(context);
    state[handle.index] = 2;
  };
  for (std::uint32_t i = 0; i < m_views.size(); ++i) {
    if (auto *outputView = dynamic_cast<IOutputView *>(m_views[i].value.get()))
      build({i, m_views[i].generation, m_ownerId});
  }
  graph.Compile();
  m_renderGraph = std::move(graph);
}
void Renderer::Render() {

    for (const auto &viewSlot : m_views) {
        if (auto *outputView = dynamic_cast<IOutputView *>(viewSlot.value.get())) {
           outputView->Begin(this);
        }
    }
    m_renderGraph.Execute(this, &m_renderGraphAllocator);
    for (const auto &viewSlot : m_views) {
        if (auto *outputView = dynamic_cast<IOutputView *>(viewSlot.value.get())) {
           outputView->End(this);
        }
    }
}

} // namespace Lighthouse
