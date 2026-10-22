#include <Lighthouse/Engine/Assets/Mesh.hpp>

#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace Lighthouse {
namespace {
template <typename Vertex>
void SetGeometry(RenderMeshBuilder& builder, const std::vector<Vertex>& vertices,
                 const std::vector<std::uint32_t>& indices, PrimitiveTopology topology) {
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
        indices.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Mesh vertex or index count exceeds 32-bit range");
    for (const auto index : indices) {
        if (index >= vertices.size())
            throw std::invalid_argument("Mesh index is outside the vertex array");
    }
    builder.Topology(topology)
        .VertexStride(sizeof(Vertex))
        .VertexCount(static_cast<std::uint32_t>(vertices.size()))
        .VertexData(AsBytes(vertices));
    if (!indices.empty()) {
        builder.IndexData(AsBytes(indices), IndexFormat::UInt32)
            .IndexCount(static_cast<std::uint32_t>(indices.size()));
    }
}
} // namespace

RenderMesh Mesh2D::ToRenderMesh() const {
    RenderMeshBuilder builder;
    builder.Attribute("POSITION", VertexFormat::Float2, 0, 0, offsetof(Mesh2DVertex, position))
        .Attribute("TEXCOORD0", VertexFormat::Float2, 1, 0, offsetof(Mesh2DVertex, uv));
    SetGeometry(builder, vertices, indices, topology);
    return builder.Build();
}

RenderMesh Mesh3D::ToRenderMesh() const {
    RenderMeshBuilder builder;
    builder.Attribute("POSITION", VertexFormat::Float3, 0, 0, offsetof(Mesh3DVertex, position))
        .Attribute("NORMAL", VertexFormat::Float3, 1, 0, offsetof(Mesh3DVertex, normal))
        .Attribute("TEXCOORD0", VertexFormat::Float2, 2, 0, offsetof(Mesh3DVertex, uv));
    SetGeometry(builder, vertices, indices, topology);
    return builder.Build();
}

} // namespace Lighthouse
