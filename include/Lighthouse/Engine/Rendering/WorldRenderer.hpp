#pragma once

#include <Lighthouse/Engine/Rendering/RenderScene.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>

namespace Lighthouse {

class WorldRenderer {
public:
    void Extract(const Scene& scene, RenderScene& output) const;
};

} // namespace Lighthouse
