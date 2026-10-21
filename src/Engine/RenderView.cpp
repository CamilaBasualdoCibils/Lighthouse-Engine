#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <functional>
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
const std::vector<RenderTechniqueHandle>& IRenderView::TechniqueDependencies(RenderTechniqueHandle handle) const {
    const auto* slot = FindSlot(handle);
    if (!slot) throw std::logic_error("Technique reference is invalid");
    return slot->dependencies;
}
std::vector<RenderTechniqueHandle> IRenderView::Techniques() const {
    std::vector<RenderTechniqueHandle> result;
    for (std::uint32_t i = 0; i < m_techniques.size(); ++i)
        if (m_techniques[i].value) result.push_back({i, m_techniques[i].generation, m_ownerId});
    return result;
}
std::vector<RenderTechniqueHandle> IRenderView::TechniqueOrder() const {
    std::vector<RenderTechniqueHandle> result;
    std::vector<unsigned char> state(m_techniques.size());
    std::function<void(RenderTechniqueHandle)> visit = [&](RenderTechniqueHandle handle) {
        const auto* slot = FindSlot(handle);
        if (!slot) throw std::logic_error("Missing, invalidated, or foreign technique dependency");
        if (state[handle.index] == 1) throw std::logic_error("Cyclic technique dependency");
        if (state[handle.index] == 2) return;
        state[handle.index] = 1;
        for (auto dependency : slot->dependencies) visit(dependency);
        state[handle.index] = 2;
        result.push_back(handle);
    };
    for (auto handle : Techniques()) visit(handle);
    return result;
}

GraphImageHandle IRenderView::BuildGraph(ViewBuildContext& ctx) {
    return BuildTechniques(ctx);
}

GraphImageHandle IRenderView::BuildTechniques(ViewBuildContext& ctx) {
    const auto order = TechniqueOrder();
    if (FinalOutput() && !GetTechnique(FinalOutput()))
        throw std::logic_error("Final output technique is invalid");
    if (!order.empty() && !FinalOutput())
        throw std::logic_error("Final output technique is not selected");

    std::unordered_map<std::uint32_t, std::any> outputs;
    std::optional<GraphImageHandle> finalImage;
    for (auto handle : order) {
        TechniqueBuildContext techniqueContext(ctx.graph, *this, outputs, handle,
            &TechniqueDependencies(handle), &ViewDependencies(), &ctx.viewOutputs, ctx.windows);
        GetTechnique(handle)->BuildGraph(techniqueContext);
        if (handle == FinalOutput()) finalImage = techniqueContext.FinalImage();
    }
    if (FinalOutput() && !finalImage)
        throw std::logic_error("Final output image is missing");
    return finalImage.value_or(InvalidGraphImage);
}

GraphImageHandle WindowView::BuildGraph(ViewBuildContext& ctx) {
    if (!ctx.windows || !ctx.windows->GetWindow(window))
        throw std::logic_error("Window view targets an invalid window");
    if (!ctx.device) throw std::logic_error("Window view requires a render device");
    if (!swapchain || !ctx.device->IsValid(swapchain))
        swapchain = ctx.device->CreateSwapchain(*ctx.windows->GetWindow(window), presentation);

    const auto image = BuildTechniques(ctx);
    if (Techniques().empty()) return InvalidGraphImage;
    if (image != InvalidGraphImage)
        throw std::logic_error("Connecting a graph image to a window is unsupported");
    ctx.graph.AddPass("present window",
        [](RenderGraph::Builder& builder, auto&) { builder.SetSideEffect(); },
        [target = swapchain](const auto&, FrameGraphPassResources&, IRenderDevice& device) {
            device.Present(target);
        });
    return InvalidGraphImage;
}

GraphImageHandle ExportView::BuildGraph(ViewBuildContext& ctx) {
    if (!image || !imageView)
        throw std::logic_error("Export view targets an invalid external image");
    const auto finalImage = BuildTechniques(ctx);
    if (!Techniques().empty())
        throw std::logic_error("Copying a graph image to an external image is unsupported");
    return finalImage;
}

GraphImageHandle RenderTextureView::BuildGraph(ViewBuildContext& ctx) {
    const auto image = BuildTechniques(ctx);
    if (image == InvalidGraphImage)
        throw std::logic_error("Transient dependency view has no output");
    return image;
}

} // namespace Lighthouse
