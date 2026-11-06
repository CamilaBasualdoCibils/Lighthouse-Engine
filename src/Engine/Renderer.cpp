#include <Lighthouse/Engine/Core/Profiling.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>

#include "Lighthouse/Engine/Rendering/RenderView.hpp"
#include "RenderMeshManager.hpp"
#include <Lighthouse/Engine/Assets/Mesh.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace Lighthouse {

Renderer::Renderer(std::unique_ptr<IRenderDevice> device,
                   WindowManager *windows)
    : m_windows(windows), m_device(std::move(device)),
      m_renderGraphAllocator(*m_device.get()),
      m_renderMeshManager(std::make_unique<RenderMeshManager>()) {
  static std::atomic<std::uint64_t> nextOwner{1};
  m_ownerId = nextOwner.fetch_add(1, std::memory_order_relaxed);
}
Renderer::~Renderer() {
  m_techniques.clear();
  m_renderMeshManager->Release(*m_device);
#ifdef LIGHTHOUSE_IMGUI
  // ImGui owns native contexts and must release them before engine windows.
  for (std::uint32_t i = 0; i < m_views.size(); ++i)
    if (dynamic_cast<ImGuiView *>(m_views[i].value.get()))
      DestroyView({i, m_views[i].generation, m_ownerId});
#endif
  for (std::uint32_t i = 0; i < m_views.size(); ++i)
    if (m_views[i].value)
      DestroyView({i, m_views[i].generation, m_ownerId});
}

Renderer::TechniqueSlot *
Renderer::FindSlot(RenderTechniqueHandle handle) noexcept {
  if (!handle || handle.owner != m_ownerId ||
      handle.index >= m_techniques.size())
    return nullptr;
  auto &slot = m_techniques[handle.index];
  return slot.value && slot.generation == handle.generation ? &slot : nullptr;
}
const Renderer::TechniqueSlot *
Renderer::FindSlot(RenderTechniqueHandle handle) const noexcept {
  return const_cast<Renderer *>(this)->FindSlot(handle);
}
IRenderTechnique *
Renderer::GetTechnique(RenderTechniqueHandle handle) noexcept {
  auto *slot = FindSlot(handle);
  return slot ? slot->value.get() : nullptr;
}
const IRenderTechnique *
Renderer::GetTechnique(RenderTechniqueHandle handle) const noexcept {
  auto *slot = FindSlot(handle);
  return slot ? slot->value.get() : nullptr;
}
bool Renderer::RemoveTechnique(RenderTechniqueHandle handle) {
  auto *slot = FindSlot(handle);
  if (!slot)
    return false;
  slot->value.reset();
  if (++slot->generation == 0)
    ++slot->generation;
  return true;
}

std::vector<RenderTechniqueHandle> Renderer::Techniques() const {
  std::vector<RenderTechniqueHandle> result;
  for (std::uint32_t i = 0; i < m_techniques.size(); ++i)
    if (m_techniques[i].value)
      result.push_back({i, m_techniques[i].generation, m_ownerId});
  return result;
}

RenderViewHandle Renderer::AddView(std::unique_ptr<IRenderView> value) {
  std::uint32_t index = 0;
  while (index < m_views.size() && m_views[index].value)
    ++index;
  if (index == m_views.size())
    m_views.emplace_back();
  m_views[index].value = std::move(value);
  return {index, m_views[index].generation, m_ownerId};
}

RenderViewHandle Renderer::CreateWindowView(const RenderWindowDesc &description,

                                            SwapchainConfig presentation) {
  if (!m_windows)
    throw std::logic_error("Creating a window view requires a WindowManager");
  const auto window = m_windows->CreateWindow(WindowDesc{
      .title = description.title,
      .width = description.width,
      .height = description.height,
      .mode = description.mode,
      .eglSurface = m_device->RequiresEGLWindow(),
      .resizable = description.resizable,
      .visible = description.visible,
      .decorated = description.decorated,
  });
  SwapchainHandle swapchain{};
  try {
    auto view =
        std::unique_ptr<WindowView>(new WindowView(window, presentation));
    view->m_windows = m_windows;
    // OpenGL resource creation requires the EGL context owned by a swapchain.
    swapchain = m_device->CreateSwapchain(*m_windows->GetWindow(window), presentation);
    view->swapchain = swapchain;
    return AddView(std::move(view));
  } catch (...) {
    if (swapchain)
      m_device->DestroySwapchain(swapchain);
    m_windows->DestroyWindow(window);
    throw;
  }
}

RenderMeshHandle Renderer::UploadMesh(const Mesh& mesh) {
  return UploadMesh(mesh.ToRenderMesh());
}
RenderMeshHandle Renderer::UploadMesh(const RenderMesh& mesh) {
    LIGHTHOUSE_PROFILE_SCOPE("Renderer::UploadMesh");
  return m_renderMeshManager->Upload(mesh, *m_device);
}

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
  auto *windowView = dynamic_cast<WindowView *>(view);
  const auto window = windowView ? windowView->window : WindowHandle{};
  if (windowView && windowView->swapchain && m_device)
    m_device->DestroySwapchain(windowView->swapchain);
  auto &slot = m_views[handle.index];
  slot.value.reset();
  if (window && m_windows)
    m_windows->DestroyWindow(window);
  if (++slot.generation == 0)
    ++slot.generation;
  return true;
}

void Renderer::Compile() {
    LIGHTHOUSE_PROFILE_SCOPE("Renderer::Compile");
  RenderGraph graph;

  for (std::uint32_t i = 0; i < m_views.size(); ++i) {
    if (auto *view = dynamic_cast<IRenderView *>(m_views[i].value.get())) {
      ViewBuildContext context{graph, *m_device.get()};
      view->BuildGraph(context);
    }
  }
  for (auto handle : Techniques()) {
    TechniqueBuildContext context{graph, *this, *m_device};
    GetTechnique(handle)->BuildGraph(context);
  }
  graph.Compile();
  m_renderGraph = std::move(graph);
}
void Renderer::Render() {
    LIGHTHOUSE_PROFILE_SCOPE("Renderer::Render");

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
