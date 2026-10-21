#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderSession.hpp>
#include <Lighthouse/Engine/Rendering/RenderTechnique.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Rendering/Mesh.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

namespace Lighthouse {

class IRenderDevice;
class MeshManager;

class Renderer {
public:
    explicit Renderer(WindowManager* windows = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Attach the device that owns buffers referenced by meshes created below.
    // The device must stay alive while querying its buffer-backed meshes; clear
    // this pointer before destroying or replacing the device.
    void SetRenderDevice(IRenderDevice* device) noexcept { m_device = device; }
    IRenderDevice* GetRenderDevice() noexcept { return m_device; }
    const IRenderDevice* GetRenderDevice() const noexcept { return m_device; }

    MeshHandle CreateMesh(const MeshDesc& description);
    MeshHandle CreateMesh(const MeshBuilder& builder) { return CreateMesh(builder.Build()); }
    bool DestroyMesh(MeshHandle mesh);
    const MeshDesc* GetMeshDesc(MeshHandle mesh) const noexcept;
    bool IsValid(MeshHandle mesh) const noexcept;
    MeshUploadStatus GetMeshUploadStatus(MeshHandle mesh) const noexcept;

    RenderViewHandle CreateView(const RenderViewDesc& desc = {});
    bool DestroyView(RenderViewHandle view);
    RenderView* GetView(RenderViewHandle view) noexcept;
    const RenderView* GetView(RenderViewHandle view) const noexcept;

    RenderSessionHandle CreateSession(const RenderSessionDesc& desc = {});
    bool DestroySession(RenderSessionHandle session);
    RenderSession* GetSession(RenderSessionHandle session) noexcept;
    const RenderSession* GetSession(RenderSessionHandle session) const noexcept;
    bool IsSessionValid(RenderSessionHandle session) const noexcept;

    RenderTechniqueHandle RegisterTechnique(std::unique_ptr<IRenderTechnique> technique);
    bool UnregisterTechnique(RenderTechniqueHandle technique);
    IRenderTechnique* GetTechnique(RenderTechniqueHandle technique) noexcept;
    const IRenderTechnique* GetTechnique(RenderTechniqueHandle technique) const noexcept;

private:
    template <typename T> struct Slot { std::uint32_t generation{1}; std::unique_ptr<T> value; };
    template <typename T, typename Tag> static Handle<Tag> Add(std::vector<Slot<T>>& slots, std::unique_ptr<T> value);
    template <typename T, typename Tag> static bool Remove(std::vector<Slot<T>>& slots, Handle<Tag> handle);
    template <typename T, typename Tag> static T* Find(std::vector<Slot<T>>& slots, Handle<Tag> handle) noexcept;
    template <typename T, typename Tag> static const T* Find(const std::vector<Slot<T>>& slots, Handle<Tag> handle) noexcept;

    WindowManager* m_windows{};
    std::vector<Slot<RenderView>> m_views;
    std::vector<Slot<RenderSession>> m_sessions;
    std::vector<Slot<IRenderTechnique>> m_techniques;
    IRenderDevice* m_device{};
    std::unique_ptr<MeshManager> m_meshManager;
};

} // namespace Lighthouse
