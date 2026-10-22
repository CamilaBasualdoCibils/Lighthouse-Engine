#include "Lighthouse/Engine/Scene/Entity.hpp"
#include <Lighthouse/Engine/Rendering/WorldRenderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <utility>

#include <Lighthouse/Engine/Scene/MeshRenderer.hpp>
#include <Lighthouse/Engine/Scene/Transform.hpp>

namespace Lighthouse {
namespace {} // namespace

void WorldRenderer::Extract(const Scene &scene, RenderScene &output) const {
  output.Clear();
  if (!scene.m_registry)
    return;

  auto view = scene.m_registry->view<const MeshRenderer>();
  for (const auto entity : view) {
    const auto &renderer = view.get<const MeshRenderer>(entity);
    if (!renderer.mesh)
      continue;

    MeshInstance instance;
    instance.mesh = renderer.mesh;
    instance.material = renderer.material;
    if (scene.m_registry->all_of<Transform>(entity)) {
      const Transform &transform =
          scene.m_registry->get<const Transform>(entity);
      instance.transform = transform.GetMatrix();

      std::optional<EntityID> parent = transform.GetParent();
      while (parent.has_value()) {
        const Transform &parentTransform =
            scene.m_registry->get<const Transform>(*parent);
        instance.transform = parentTransform.GetMatrix() * instance.transform;
        parent = parentTransform.GetParent();
      }
    }

    // MeshRenderer does not yet assign a graphics pipeline, so extracted
    // instances use the invalid/default pipeline key until that is defined.
    output.meshes.emplace(GraphicsPipelineHandle{}, std::move(instance));
  }
}

} // namespace Lighthouse
