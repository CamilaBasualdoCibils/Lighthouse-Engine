#include <Lighthouse/Engine/Renderer.hpp>

#include <algorithm>

namespace Engine {

template <typename T, typename Tag>
Handle<Tag> Renderer::Add(std::vector<Slot<T>>& slots, std::unique_ptr<T> value) {
    std::uint32_t index = 0;
    while (index < slots.size() && slots[index].value) ++index;
    if (index == slots.size()) slots.emplace_back();
    slots[index].value = std::move(value);
    return {index, slots[index].generation};
}

template <typename T, typename Tag>
bool Renderer::Remove(std::vector<Slot<T>>& slots, Handle<Tag> handle) {
    if (!handle || handle.index >= slots.size()) return false;
    auto& slot = slots[handle.index];
    if (!slot.value || slot.generation != handle.generation) return false;
    slot.value.reset();
    if (++slot.generation == 0) ++slot.generation;
    return true;
}

template <typename T, typename Tag>
T* Renderer::Find(std::vector<Slot<T>>& slots, Handle<Tag> handle) noexcept {
    if (!handle || handle.index >= slots.size()) return nullptr;
    auto& slot = slots[handle.index];
    return slot.generation == handle.generation ? slot.value.get() : nullptr;
}

template <typename T, typename Tag>
const T* Renderer::Find(const std::vector<Slot<T>>& slots, Handle<Tag> handle) noexcept {
    if (!handle || handle.index >= slots.size()) return nullptr;
    const auto& slot = slots[handle.index];
    return slot.generation == handle.generation ? slot.value.get() : nullptr;
}

RenderViewHandle Renderer::CreateView(const RenderViewDesc& desc) {
    return Add<RenderView, RenderViewHandleTag>(m_views, std::make_unique<RenderView>(desc));
}
bool Renderer::DestroyView(RenderViewHandle view) { return Remove(m_views, view); }
RenderView* Renderer::GetView(RenderViewHandle view) noexcept { return Find(m_views, view); }
const RenderView* Renderer::GetView(RenderViewHandle view) const noexcept { return Find(m_views, view); }

RenderSessionHandle Renderer::CreateSession(const RenderSessionDesc& desc) {
    return Add<RenderSession, RenderSessionHandleTag>(m_sessions, std::make_unique<RenderSession>(desc));
}
bool Renderer::DestroySession(RenderSessionHandle session) { return Remove(m_sessions, session); }
RenderSession* Renderer::GetSession(RenderSessionHandle session) noexcept { return Find(m_sessions, session); }
const RenderSession* Renderer::GetSession(RenderSessionHandle session) const noexcept { return Find(m_sessions, session); }

bool Renderer::IsSessionValid(RenderSessionHandle handle) const noexcept {
    const auto* session = GetSession(handle);
    if (!session || !session->GetView() || !session->GetOutput().IsSet()) return false;
    const auto* view = GetView(session->GetView());
    if (!view || !view->HasValidReferences()) return false;
    if (view->GetTechnique() && !GetTechnique(view->GetTechnique())) return false;
    if (const auto* window = std::get_if<WindowHandle>(&session->GetOutput().target)) {
        return m_windows && m_windows->GetWindow(*window);
    }
    return std::get<OffscreenTargetHandle>(session->GetOutput().target).IsValid();
}

RenderTechniqueHandle Renderer::RegisterTechnique(std::unique_ptr<IRenderTechnique> technique) {
    if (!technique) return {};
    return Add<IRenderTechnique, RenderTechniqueHandleTag>(m_techniques, std::move(technique));
}
bool Renderer::UnregisterTechnique(RenderTechniqueHandle technique) { return Remove(m_techniques, technique); }
IRenderTechnique* Renderer::GetTechnique(RenderTechniqueHandle technique) noexcept { return Find(m_techniques, technique); }
const IRenderTechnique* Renderer::GetTechnique(RenderTechniqueHandle technique) const noexcept { return Find(m_techniques, technique); }

} // namespace Engine
