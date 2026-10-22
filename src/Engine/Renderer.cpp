#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include "RenderMeshManager.hpp"

#include <algorithm>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace Lighthouse {

Renderer::Renderer(WindowManager* windows)
    : m_windows(windows), m_renderMeshManager(std::make_unique<RenderMeshManager>()) {
    static std::atomic<std::uint64_t> nextOwner{1};
    m_ownerId = nextOwner.fetch_add(1, std::memory_order_relaxed);
}
Renderer::~Renderer() { ResetRenderDevice(); }
void Renderer::SetRenderDevice(std::unique_ptr<IRenderDevice> device) {
    if (!device) throw std::invalid_argument("Render device cannot be null");
    if (m_device) throw std::logic_error("Renderer already owns a render device");
    m_device = std::move(device);
}
void Renderer::ResetRenderDevice() noexcept {
    if (m_device) for (auto& slot : m_views) {
        auto* view = dynamic_cast<WindowView*>(slot.value.get());
        if (!view || !view->swapchain) continue;
        try { m_device->DestroySwapchain(view->swapchain); } catch (...) {}
        view->swapchain = {};
    }
    m_device.reset();
}
RenderMeshHandle Renderer::CreateRenderMesh(const RenderMesh& description) { return m_renderMeshManager->Create(description, m_device.get()); }
bool Renderer::DestroyRenderMesh(RenderMeshHandle mesh) { return m_renderMeshManager->Destroy(mesh); }
const RenderMesh* Renderer::GetRenderMesh(RenderMeshHandle mesh) const noexcept { return m_renderMeshManager->GetDescription(mesh, m_device.get()); }
bool Renderer::IsValid(RenderMeshHandle mesh) const noexcept { return m_renderMeshManager->IsValid(mesh, m_device.get()); }
RenderMeshUploadStatus Renderer::GetRenderMeshUploadStatus(RenderMeshHandle mesh) const noexcept { return m_renderMeshManager->GetUploadStatus(mesh, m_device.get()); }

IRenderView* Renderer::GetView(RenderViewHandle handle) noexcept {
    if (!handle || handle.owner != m_ownerId || handle.index >= m_views.size()) return nullptr;
    auto& slot = m_views[handle.index];
    return slot.generation == handle.generation ? slot.value.get() : nullptr;
}
const IRenderView* Renderer::GetView(RenderViewHandle handle) const noexcept {
    return const_cast<Renderer*>(this)->GetView(handle);
}
bool Renderer::DestroyView(RenderViewHandle handle) {
    auto* view = GetView(handle);
    if (!view) return false;
    if (auto* window = dynamic_cast<WindowView*>(view); window && window->swapchain && m_device)
        m_device->DestroySwapchain(window->swapchain);
    auto& slot = m_views[handle.index];
    slot.value.reset();
    if (++slot.generation == 0) ++slot.generation;
    return true;
}

RenderGraph Renderer::Compile(std::span<const OutputViewHandle> roots) {
    RenderGraph graph;
    std::vector<unsigned char> state(m_views.size());
    std::function<void(RenderViewHandle)> build = [&](RenderViewHandle handle) {
        auto* view = GetView(handle);
        if (!view) throw std::logic_error("View dependency is missing or invalidated");
        if (!view->HasValidReferences()) throw std::logic_error("View scene or camera is invalid");
        if (state[handle.index] == 1) throw std::logic_error("Cyclic view dependency");
        if (state[handle.index] == 2) return;
        state[handle.index] = 1;
        for (auto dependency : view->ViewDependencies()) build(dependency);

        ViewBuildContext context{graph, m_device.get()};
        view->BuildGraph(context);
        state[handle.index] = 2;
    };
    for (auto root : roots) {
        if (!dynamic_cast<IOutputView*>(GetView(root)))
            throw std::logic_error("Compilation root must be a valid output view");
        build(root);
    }
    graph.Compile();
    return graph;
}

} // namespace Lighthouse
