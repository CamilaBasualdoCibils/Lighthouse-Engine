#include <Lighthouse/Engine/Rendering/WorldRenderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <utility>

#include <Lighthouse/Engine/Scene/MeshRenderer.hpp>
#include <Lighthouse/Engine/Scene/Transform.hpp>

namespace Lighthouse {
namespace {

Mat4 ToMatrix(const Transform& transform) {
    Mat4 result{1.0f};
    result = glm::translate(result, transform.position);
    result = glm::rotate(result, transform.rotation.x, Vec3{1.0f, 0.0f, 0.0f});
    result = glm::rotate(result, transform.rotation.y, Vec3{0.0f, 1.0f, 0.0f});
    result = glm::rotate(result, transform.rotation.z, Vec3{0.0f, 0.0f, 1.0f});
    return glm::scale(result, transform.scale);
}

} // namespace

void WorldRenderer::Extract(const Scene& scene, RenderScene& output) const {
    output.Clear();
    if (!scene.m_registry) return;

    auto view = scene.m_registry->view<const MeshRenderer>();
    for (const auto entity : view) {
        const auto& renderer = view.get<const MeshRenderer>(entity);
        if (!renderer.mesh) continue;

        MeshInstance instance;
        instance.mesh = renderer.mesh;
        instance.material = renderer.material;
        if (scene.m_registry->all_of<Transform>(entity)) {
            instance.transform = ToMatrix(scene.m_registry->get<const Transform>(entity));
        }

        // MeshRenderer does not yet assign a graphics pipeline, so extracted
        // instances use the invalid/default pipeline key until that is defined.
        output.meshes.emplace(GraphicsPipelineHandle{}, std::move(instance));
    }
}

} // namespace Lighthouse
