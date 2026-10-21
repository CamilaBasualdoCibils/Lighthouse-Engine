#pragma once

#include <memory>

namespace Engine {

class Mesh;
class Material;

struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;

    MeshRenderer(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material);
};

} // namespace Engine
