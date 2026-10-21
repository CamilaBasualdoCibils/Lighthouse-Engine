#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Lighthouse/Engine/Rendering/Mesh.hpp>

namespace Lighthouse {

class IRenderDevice;

class MeshManager {
public:
    MeshHandle Create(const MeshDesc& description, IRenderDevice* device);
    bool Destroy(MeshHandle mesh);
    const MeshDesc* GetDescription(MeshHandle mesh, IRenderDevice* device) const noexcept;
    bool IsValid(MeshHandle mesh, IRenderDevice* device) const noexcept;
    MeshUploadStatus GetUploadStatus(MeshHandle mesh, IRenderDevice* device) const noexcept;

private:
    struct Entry {
        MeshDesc description;
    };
    struct Slot {
        std::uint32_t generation{1};
        std::unique_ptr<Entry> value;
    };
    std::vector<Slot> m_slots;
};

} // namespace Lighthouse
