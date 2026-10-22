#pragma once

#include <cstdint>
#include <vector>

#include <Lighthouse/Engine/Core/Types.hpp>
#include <Lighthouse/Engine/Rendering/RenderMesh.hpp>

namespace Lighthouse {

class Mesh {
public:
    virtual ~Mesh() = default;
    virtual RenderMesh ToRenderMesh() const = 0;
};

struct Mesh2DVertex {
    Vec2 position{};
    Vec2 uv{};
};

class Mesh2D final : public Mesh {
public:
    std::vector<Mesh2DVertex> vertices;
    std::vector<std::uint32_t> indices;
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};

    RenderMesh ToRenderMesh() const override;
};

struct Mesh3DVertex {
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
};

class Mesh3D final : public Mesh {
public:
    std::vector<Mesh3DVertex> vertices;
    std::vector<std::uint32_t> indices;
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};

    RenderMesh ToRenderMesh() const override;
};

} // namespace Lighthouse
