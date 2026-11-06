#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderMesh.hpp>

namespace Lighthouse {

class IRenderDevice;

class RenderMeshManager {
public:
    RenderMeshManager();
    RenderMeshHandle Upload(const RenderMesh& description, IRenderDevice& device);
    void Release(IRenderDevice& device);
    RenderMeshHandle Create(const RenderMesh& description, IRenderDevice* device);
    bool Destroy(RenderMeshHandle mesh);
    const RenderMesh* GetDescription(RenderMeshHandle mesh, IRenderDevice* device) const noexcept;
    bool IsValid(RenderMeshHandle mesh, IRenderDevice* device) const noexcept;
    RenderMeshUploadStatus GetUploadStatus(RenderMeshHandle mesh, IRenderDevice* device) const noexcept;

private:
    struct Entry {
        RenderMesh description;
        bool pooled{};
    };
    struct Slot {
        std::uint32_t generation{1};
        std::unique_ptr<Entry> value;
    };
    std::vector<Slot> m_slots;
    std::uint64_t m_ownerId{};
    BufferHandle m_vertexBuffer{}, m_indexBuffer{};
    std::vector<std::byte> m_vertices;
    std::vector<std::uint32_t> m_indices;
};

} // namespace Lighthouse
