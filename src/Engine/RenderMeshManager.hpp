#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderMesh.hpp>

namespace Lighthouse {

class IRenderDevice;

class RenderMeshManager {
public:
    RenderMeshHandle Create(const RenderMesh& description, IRenderDevice* device);
    bool Destroy(RenderMeshHandle mesh);
    const RenderMesh* GetDescription(RenderMeshHandle mesh, IRenderDevice* device) const noexcept;
    bool IsValid(RenderMeshHandle mesh, IRenderDevice* device) const noexcept;
    RenderMeshUploadStatus GetUploadStatus(RenderMeshHandle mesh, IRenderDevice* device) const noexcept;

private:
    struct Entry {
        RenderMesh description;
    };
    struct Slot {
        std::uint32_t generation{1};
        std::unique_ptr<Entry> value;
    };
    std::vector<Slot> m_slots;
};

} // namespace Lighthouse
