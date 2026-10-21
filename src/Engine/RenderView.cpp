#include <Lighthouse/Engine/Rendering/RenderView.hpp>

#include <functional>

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

} // namespace Lighthouse
