#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include "MeshManager.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace Lighthouse {

Renderer::Renderer(WindowManager* windows)
    : m_windows(windows), m_meshManager(std::make_unique<MeshManager>()) {}
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
MeshHandle Renderer::CreateMesh(const MeshDesc& description) { return m_meshManager->Create(description, m_device.get()); }
bool Renderer::DestroyMesh(MeshHandle mesh) { return m_meshManager->Destroy(mesh); }
const MeshDesc* Renderer::GetMeshDesc(MeshHandle mesh) const noexcept { return m_meshManager->GetDescription(mesh, m_device.get()); }
bool Renderer::IsValid(MeshHandle mesh) const noexcept { return m_meshManager->IsValid(mesh, m_device.get()); }
MeshUploadStatus Renderer::GetMeshUploadStatus(MeshHandle mesh) const noexcept { return m_meshManager->GetUploadStatus(mesh, m_device.get()); }

IRenderView* Renderer::GetView(RenderViewHandle handle) noexcept {
    if (!handle || handle.index >= m_views.size()) return nullptr;
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

GraphImageHandle Renderer::BuildView(RenderGraph& graph, RenderViewHandle handle) {
    auto* view = GetView(handle);
    if (!view) throw std::logic_error("Render view handle is invalid");
    return BuildView(graph, *view);
}
GraphImageHandle Renderer::BuildView(RenderGraph& graph, IRenderView& view) {
    auto order = view.TechniqueOrder();
    if (view.FinalOutput() && !view.GetTechnique(view.FinalOutput()))
        throw std::logic_error("Final output technique is invalid");
    std::unordered_map<std::uint32_t, std::any> outputs;
    std::optional<GraphImageHandle> finalImage;
    for (auto handle : order) {
        TechniqueBuildContext context(graph, view, outputs, handle);
        view.GetTechnique(handle)->Build(context);
        if (handle == view.FinalOutput()) finalImage = context.FinalImage();
    }
    if (!view.FinalOutput()) throw std::logic_error("Final output technique is not selected");
    if (!finalImage) throw std::logic_error("Final output image is missing");
    if (auto* window = dynamic_cast<WindowView*>(&view)) {
        if (!m_windows || !m_windows->GetWindow(window->window)) throw std::logic_error("Window is invalid");
        if (!m_device) throw std::logic_error("Window view requires a render device");
        if (!window->swapchain) window->swapchain = m_device->CreateSwapchain(*m_windows->GetWindow(window->window), window->presentation);
        throw std::logic_error("Render graph presentation is unsupported by this device interface");
    }
    if (dynamic_cast<ExportView*>(&view))
        throw std::logic_error("Render graph export is unsupported by this device interface");
    return *finalImage;
}
std::vector<GraphImageHandle> Renderer::BuildViews(RenderGraph& graph,
    const std::vector<RenderViewHandle>& views,
    const std::vector<std::pair<RenderViewHandle, RenderViewHandle>>& dependencies) {
    std::vector<unsigned char> state(views.size());
    std::vector<GraphImageHandle> result(views.size());
    std::function<void(std::size_t)> visit = [&](std::size_t index) {
        if (state[index] == 1) throw std::logic_error("Cyclic view dependency");
        if (state[index] == 2) return;
        if (!GetView(views[index])) throw std::logic_error("View dependency is invalid");
        state[index] = 1;
        for (auto [consumer, producer] : dependencies) if (consumer == views[index]) {
            auto found = std::find(views.begin(), views.end(), producer);
            if (found == views.end()) throw std::logic_error("View dependency is missing");
            visit(static_cast<std::size_t>(found - views.begin()));
        }
        result[index] = BuildView(graph, views[index]);
        state[index] = 2;
    };
    for (std::size_t i = 0; i < views.size(); ++i) visit(i);
    return result;
}

} // namespace Lighthouse
