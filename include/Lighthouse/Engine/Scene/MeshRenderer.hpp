#pragma once

#include <memory>
#include <utility>

namespace Lighthouse {

class Mesh;
class Material;

struct MeshRenderer {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;

    MeshRenderer(std::shared_ptr<Mesh> mesh, std::shared_ptr<Material> material)
        : mesh(std::move(mesh)), material(std::move(material)) {}
};

} // namespace Lighthouse
