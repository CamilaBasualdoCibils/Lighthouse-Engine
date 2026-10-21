#pragma once

#include <cstdint>
#include <utility>
#include <variant>

#include <Lighthouse/Engine/Entity.hpp>
#include <Lighthouse/Engine/Handle.hpp>
#include <Lighthouse/Engine/Types.hpp>

namespace Engine {

struct ExplicitCamera {
    Vec3 position{};
    Vec3 direction{0.0f, 0.0f, 1.0f};
    float fieldOfViewDegrees{70.0f};
};

class CameraSource {
public:
    CameraSource() = default;
    CameraSource(const Entity& entity);
    CameraSource(ExplicitCamera camera) : m_source(camera) {}

    bool IsSet() const noexcept;
    bool IsValid() const noexcept;
    const std::variant<std::monostate, Entity, ExplicitCamera>& Value() const noexcept { return m_source; }

private:
    std::variant<std::monostate, Entity, ExplicitCamera> m_source;
};

struct RenderSettings {
    Vec3 clearColor{};
    bool clearColorEnabled{true};
    bool clearDepthEnabled{true};
};

using LayerMask = std::uint64_t;

struct RenderViewDesc {
    SceneHandle scene;
    CameraSource camera;
    RenderTechniqueHandle technique;
    RenderSettings settings;
    LayerMask visibleLayers{~LayerMask{0}};
};

class RenderView {
public:
    explicit RenderView(RenderViewDesc desc = {}) : m_desc(std::move(desc)) {}
    void SetScene(SceneHandle scene) noexcept { m_desc.scene = scene; }
    void SetCamera(CameraSource camera) { m_desc.camera = std::move(camera); }
    void SetTechnique(RenderTechniqueHandle technique) noexcept { m_desc.technique = technique; }
    void SetSettings(const RenderSettings& settings) { m_desc.settings = settings; }
    void SetVisibleLayers(LayerMask layers) noexcept { m_desc.visibleLayers = layers; }

    SceneHandle GetScene() const noexcept { return m_desc.scene; }
    const CameraSource& GetCamera() const noexcept { return m_desc.camera; }
    RenderTechniqueHandle GetTechnique() const noexcept { return m_desc.technique; }
    const RenderSettings& GetSettings() const noexcept { return m_desc.settings; }
    LayerMask GetVisibleLayers() const noexcept { return m_desc.visibleLayers; }
    const RenderViewDesc& Description() const noexcept { return m_desc; }
    bool HasValidReferences() const noexcept;

private:
    RenderViewDesc m_desc;
};

} // namespace Engine
