#include <Lighthouse/Engine/Rendering/RenderView.hpp>

namespace Lighthouse {

CameraSource::CameraSource(const Entity& entity) : m_source(entity) {}

bool CameraSource::IsSet() const noexcept { return !std::holds_alternative<std::monostate>(m_source); }
bool CameraSource::IsValid() const noexcept {
    if (const auto* entity = std::get_if<Entity>(&m_source)) return entity->IsValid();
    return std::holds_alternative<ExplicitCamera>(m_source);
}

bool RenderView::HasValidReferences() const noexcept {
    return static_cast<bool>(m_desc.scene) && m_desc.camera.IsValid() &&
           (!m_desc.technique || m_desc.technique.IsValid());
}

} // namespace Lighthouse
