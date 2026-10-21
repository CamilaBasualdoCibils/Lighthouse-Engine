#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <algorithm>

namespace Lighthouse {

CameraSource::CameraSource(const Entity& entity) : m_source(entity) {}
bool CameraSource::IsSet() const noexcept { return !std::holds_alternative<std::monostate>(m_source); }
bool CameraSource::IsValid() const noexcept {
    if (const auto* entity = std::get_if<Entity>(&m_source)) return entity->IsValid();
    return std::holds_alternative<ExplicitCamera>(m_source);
}

IRenderView::IRenderView(RenderViewDesc desc) : m_desc(std::move(desc)) {
    static std::atomic<std::uint64_t> next{1};
    m_ownerId = next.fetch_add(1, std::memory_order_relaxed);
}
bool IRenderView::HasValidReferences() const noexcept {
    return static_cast<bool>(m_desc.scene) && m_desc.camera.IsValid();
}
IRenderView::Slot* IRenderView::FindSlot(RenderTechniqueHandle handle) noexcept {
    if (!handle || handle.owner != m_ownerId || handle.index >= m_techniques.size()) return nullptr;
    auto& slot = m_techniques[handle.index];
    return slot.value && slot.generation == handle.generation ? &slot : nullptr;
}
const IRenderView::Slot* IRenderView::FindSlot(RenderTechniqueHandle handle) const noexcept {
    return const_cast<IRenderView*>(this)->FindSlot(handle);
}
IRenderTechnique* IRenderView::GetTechnique(RenderTechniqueHandle handle) noexcept {
    auto* slot = FindSlot(handle);
    return slot ? slot->value.get() : nullptr;
}
const IRenderTechnique* IRenderView::GetTechnique(RenderTechniqueHandle handle) const noexcept {
    auto* slot = FindSlot(handle);
    return slot ? slot->value.get() : nullptr;
}
bool IRenderView::RemoveTechnique(RenderTechniqueHandle handle) {
    auto* slot = FindSlot(handle);
    if (!slot) return false;
    slot->value.reset();
    if (++slot->generation == 0) ++slot->generation;
    return true;
}
void IRenderView::AddViewDependency(RenderViewHandle handle) {
    if (!handle) throw std::invalid_argument("View dependency is invalid");
    if (std::find(m_viewDependencies.begin(), m_viewDependencies.end(), handle) == m_viewDependencies.end())
        m_viewDependencies.push_back(handle);
}
std::vector<RenderTechniqueHandle> IRenderView::Techniques() const {
    std::vector<RenderTechniqueHandle> result;
    for (std::uint32_t i = 0; i < m_techniques.size(); ++i)
        if (m_techniques[i].value) result.push_back({i, m_techniques[i].generation, m_ownerId});
    return result;
}
void IRenderView::BuildGraph(ViewBuildContext& ctx) { BuildTechniques(ctx); }

void IRenderView::BuildTechniques(ViewBuildContext& ctx) {
    for (auto handle : Techniques()) {
        TechniqueBuildContext techniqueContext(ctx.graph, *this);
        GetTechnique(handle)->BuildGraph(techniqueContext);
    }
}

Window* WindowView::GetWindow() const noexcept {
    return m_windows ? m_windows->GetWindow(window) : nullptr;
}

void WindowView::BuildGraph(ViewBuildContext& ctx) {
    auto* targetWindow = GetWindow();
    if (!targetWindow) throw std::logic_error("Window view targets an invalid window");
    if (!ctx.device) throw std::logic_error("Window view requires a render device");
    if (!swapchain || !ctx.device->IsValid(swapchain))
        swapchain = ctx.device->CreateSwapchain(*targetWindow, presentation);
    BuildTechniques(ctx);
}

void ExportView::BuildGraph(ViewBuildContext& ctx) {
    m_exportImage = InvalidGraphImage;
    if (!image || !imageView)
        throw std::logic_error("Export view targets an invalid external image");
    m_exportImage = ctx.graph.Import<ImportedRenderGraphImage>("export image", description, {image, imageView});
    BuildTechniques(ctx);
}

void RenderTextureView::BuildGraph(ViewBuildContext& ctx) {
    BuildTechniques(ctx);
}

} // namespace Lighthouse
