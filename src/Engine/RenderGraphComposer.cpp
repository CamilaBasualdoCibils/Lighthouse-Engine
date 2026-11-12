#include <Lighthouse/Engine/Core/Profiling.hpp>
#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <algorithm>
#include <exception>

#include "Lighthouse/Engine/Rendering/RenderView.hpp"
#include "RenderMeshManager.hpp"
#include <spdlog/spdlog.h>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace Lighthouse {

RenderGraphComposer::RenderGraphComposer(std::unique_ptr<IRenderDevice> device,
                   WindowManager *windows)
    : m_windows(windows), m_device(std::move(device)),
      m_renderGraphAllocator(*m_device.get()),
      m_renderMeshManager(std::make_unique<RenderMeshManager>()) {
  static std::atomic<std::uint64_t> nextOwner{1};
  m_ownerId = nextOwner.fetch_add(1, std::memory_order_relaxed);
}
RenderGraphComposer::~RenderGraphComposer() {
  // Release graph-owned UI objects before destroying their ImGui contexts.
  m_renderGraph.Clear();
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

RenderGraphComposer::TechniqueSlot *
RenderGraphComposer::FindSlot(RenderTechniqueHandle handle) noexcept {
  if (!handle || handle.owner != m_ownerId ||
      handle.index >= m_techniques.size())
    return nullptr;
  auto &slot = m_techniques[handle.index];
  return slot.value && slot.generation == handle.generation ? &slot : nullptr;
}
const RenderGraphComposer::TechniqueSlot *
RenderGraphComposer::FindSlot(RenderTechniqueHandle handle) const noexcept {
  return const_cast<RenderGraphComposer *>(this)->FindSlot(handle);
}
IRenderTechnique *
RenderGraphComposer::GetTechnique(RenderTechniqueHandle handle) noexcept {
  auto *slot = FindSlot(handle);
  return slot ? slot->value.get() : nullptr;
}
const IRenderTechnique *
RenderGraphComposer::GetTechnique(RenderTechniqueHandle handle) const noexcept {
  auto *slot = FindSlot(handle);
  return slot ? slot->value.get() : nullptr;
}
bool RenderGraphComposer::RemoveTechnique(RenderTechniqueHandle handle) {
  auto *slot = FindSlot(handle);
  if (!slot)
    return false;
  slot->value.reset();
  if (++slot->generation == 0)
    ++slot->generation;
  return true;
}

std::vector<RenderTechniqueHandle> RenderGraphComposer::Techniques() const {
  std::vector<RenderTechniqueHandle> result;
  for (std::uint32_t i = 0; i < m_techniques.size(); ++i)
    if (m_techniques[i].value)
      result.push_back({i, m_techniques[i].generation, m_ownerId});
  return result;
}

RenderViewHandle RenderGraphComposer::AddView(std::unique_ptr<IRenderView> value) {
  std::uint32_t index = 0;
  while (index < m_views.size() && m_views[index].value)
    ++index;
  if (index == m_views.size())
    m_views.emplace_back();
  m_views[index].value = std::move(value);
  return {index, m_views[index].generation, m_ownerId};
}

RenderViewHandle RenderGraphComposer::CreateWindowView(const RenderWindowDesc &description,

                                            SwapchainConfig presentation) {
  if (!m_windows)
    throw std::logic_error("Creating a window view requires a WindowManager");
  if (description.ImGuiHost) {
#ifndef LIGHTHOUSE_IMGUI
    throw std::logic_error("ImGuiHost requires LIGHTHOUSE_IMGUI");
#else
    for (const auto& slot : m_views)
      if (auto* view = dynamic_cast<WindowView*>(slot.value.get());
          view && view->IsImGuiHost())
        throw std::logic_error("Only one ImGuiHost window is allowed per renderer");
#endif
  }
  if (description.swapchainFormats.empty())
    throw std::invalid_argument(
        "Window view requires a swapchain format preference");
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
  SurfaceHandle surface{};
  SwapchainHandle swapchain{};
  try {
    const auto available =
        m_windows->EnumerateAvailableSwapchainFormats(window, *m_device);
    surface = m_device->CreateSurface(*m_windows->GetWindow(window));
    std::exception_ptr lastError;
    for (auto format : description.swapchainFormats)
    {
      if (std::find(available.begin(), available.end(), format) ==
          available.end())
        continue;
      presentation.format = format;
      try
      {
        swapchain = m_device->CreateSwapchain(surface,
                                              presentation);
        if (swapchain)
          break;
      }
      catch (const std::runtime_error&)
      {
        lastError = std::current_exception();
      }
    }
    if (!swapchain)
    {
      if (lastError)
        std::rethrow_exception(lastError);
      throw std::runtime_error(
          "None of the requested swapchain formats is available");
    }
    auto view =
        std::unique_ptr<WindowView>(new WindowView(window, presentation));
    view->m_windows = m_windows;
    view->m_imguiHost = description.ImGuiHost;
    view->surface = surface;
    view->swapchain = swapchain;
    const auto info = m_device->GetSwapchainInfo(swapchain);
    if (!info)
      throw std::runtime_error(
          "Backend did not report the created swapchain format");
    view->m_swapchainInfo = *info;
    return AddView(std::move(view));
  } catch (...) {
    if (swapchain)
      m_device->DestroySwapchain(swapchain);
    if (surface) m_device->DestroySurface(surface);
    m_windows->DestroyWindow(window);
    throw;
  }
}

RenderMeshHandle RenderGraphComposer::UploadMesh(const Mesh& mesh) {
  return UploadMesh(mesh.ToRenderMesh());
}

RenderMeshHandle RenderGraphComposer::UploadMesh(const RenderMesh& mesh) {
    LIGHTHOUSE_PROFILE_SCOPE("RenderGraphComposer::UploadMesh");
  return m_renderMeshManager->Upload(mesh, *m_device);
}

RenderMeshHandle RenderGraphComposer::CreateRenderMesh(const RenderMesh &description) {
  return m_renderMeshManager->Create(description, m_device.get());
}
bool RenderGraphComposer::DestroyRenderMesh(RenderMeshHandle mesh) {
  return m_renderMeshManager->Destroy(mesh);
}
const RenderMesh *
RenderGraphComposer::GetRenderMesh(RenderMeshHandle mesh) const noexcept {
  return m_renderMeshManager->GetDescription(mesh, m_device.get());
}
bool RenderGraphComposer::IsValid(RenderMeshHandle mesh) const noexcept {
  return m_renderMeshManager->IsValid(mesh, m_device.get());
}
RenderMeshUploadStatus
RenderGraphComposer::GetRenderMeshUploadStatus(RenderMeshHandle mesh) const noexcept {
  return m_renderMeshManager->GetUploadStatus(mesh, m_device.get());
}

IRenderView *RenderGraphComposer::GetView(RenderViewHandle handle) noexcept {
  if (!handle || handle.owner != m_ownerId || handle.index >= m_views.size())
    return nullptr;
  auto &slot = m_views[handle.index];
  return slot.generation == handle.generation ? slot.value.get() : nullptr;
}
const IRenderView *RenderGraphComposer::GetView(RenderViewHandle handle) const noexcept {
  return const_cast<RenderGraphComposer *>(this)->GetView(handle);
}
bool RenderGraphComposer::DestroyView(RenderViewHandle handle) {
  auto *view = GetView(handle);
  if (!view)
    return false;
  auto *windowView = dynamic_cast<WindowView *>(view);
  const auto window = windowView ? windowView->window : WindowHandle{};
#ifdef LIGHTHOUSE_IMGUI
  if (windowView && windowView->IsImGuiHost()) {
    m_renderGraph.Clear();
    for (const auto& slot : m_views)
      if (auto* ui = dynamic_cast<ImGuiView*>(slot.value.get()))
        ui->SetHost(nullptr);
  }
#endif
  if (windowView && windowView->swapchain && m_device)
    m_device->DestroySwapchain(windowView->swapchain);
  if (windowView && windowView->surface && m_device)
    m_device->DestroySurface(windowView->surface);
  auto &slot = m_views[handle.index];
  slot.value.reset();
  if (window && m_windows)
    m_windows->DestroyWindow(window);
  if (++slot.generation == 0)
    ++slot.generation;
  return true;
}

void RenderGraphComposer::Compile() {
  spdlog::info("Compiling Render Graph");
    LIGHTHOUSE_PROFILE_SCOPE("RenderGraphComposer::Compile");
  RenderGraph graph;
#ifdef LIGHTHOUSE_IMGUI
  WindowView* host = nullptr;
  std::vector<ImGuiView*> uiViews;
  for (const auto& slot : m_views) {
    if (auto* view = dynamic_cast<WindowView*>(slot.value.get());
        view && view->IsImGuiHost()) host = view;
    if (auto* ui = dynamic_cast<ImGuiView*>(slot.value.get())) uiViews.push_back(ui);
  }
  if (host && uiViews.size() > 1)
    throw std::logic_error("An ImGuiHost supports one ImGuiView per renderer");
  // Retire GPU work before changing context ownership or host windows.
  m_renderGraph.Drain();
  for (auto* ui : uiViews) ui->SetHost(host);
#endif

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
  for (const auto& viewSlot : m_views) {
    if (auto* output = dynamic_cast<IOutputView*>(viewSlot.value.get())) {
      ViewBuildContext context{graph, *m_device};
      output->PostBuildGraph(context);
    }
  }
  graph.Compile();
  m_renderGraph = std::move(graph);
  graph.Clear(); // The moved-out graph owns the previous callbacks and views.
  m_renderGraphAllocator.TrimUnused();
}
void RenderGraphComposer::Render() {
  // Retire the single frame's storage and acquire semaphore before reusing it.
  m_renderGraph.Drain();
    LIGHTHOUSE_PROFILE_SCOPE("RenderGraphComposer::Render");

  for (const auto &viewSlot : m_views) {
    if (auto *outputView = dynamic_cast<IOutputView *>(viewSlot.value.get())) {
      outputView->Begin(this);
    }
  }
  try {
    m_renderGraph.Execute(this, &m_renderGraphAllocator);
  } catch (...) {
#ifdef LIGHTHOUSE_IMGUI
    for (const auto& viewSlot : m_views)
      if (auto* imgui = dynamic_cast<ImGuiView*>(viewSlot.value.get())) imgui->End(this);
#endif
    throw;
  }
  for (const auto &viewSlot : m_views) {
    if (auto *outputView = dynamic_cast<IOutputView *>(viewSlot.value.get())) {
      outputView->End(this);
    }
  }
}

} // namespace Lighthouse
