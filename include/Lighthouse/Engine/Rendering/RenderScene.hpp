#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Assets/Material.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>
#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>

namespace Lighthouse {

struct MeshInstance {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    Mat4 transform{1.0f};
};

// Placeholder for future light extraction.
struct LightInstance {};

// Placeholder for future camera extraction.
struct CameraInstance {};

class RenderScene {
public:
    std::unordered_multimap<GraphicsPipelineHandle, MeshInstance> meshes;
    std::vector<LightInstance> lights;
    std::vector<CameraInstance> cameras;

    void Clear() {
        meshes.clear();
        lights.clear();
        cameras.clear();
    }
};

} // namespace Lighthouse
