#pragma once

#include <cstdint>
#include <span>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Rendering/RenderMesh.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

namespace Lighthouse {

class IRenderDevice;
class RenderMeshManager;
using OutputViewHandle = RenderViewHandle;

class Renderer {
public:
    explicit Renderer(WindowManager* windows = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void SetRenderDevice(std::unique_ptr<IRenderDevice> device);
    void ResetRenderDevice() noexcept;
    IRenderDevice* GetRenderDevice() noexcept { return m_device.get(); }
    const IRenderDevice* GetRenderDevice() const noexcept { return m_device.get(); }

    RenderMeshHandle CreateRenderMesh(const RenderMesh& description);
    RenderMeshHandle CreateRenderMesh(const RenderMeshBuilder& builder) { return CreateRenderMesh(builder.Build()); }
    bool DestroyRenderMesh(RenderMeshHandle mesh);
    const RenderMesh* GetRenderMesh(RenderMeshHandle mesh) const noexcept;
    bool IsValid(RenderMeshHandle mesh) const noexcept;
    RenderMeshUploadStatus GetRenderMeshUploadStatus(RenderMeshHandle mesh) const noexcept;

    template <typename T, typename... Args> RenderViewHandle CreateView(Args&&... args) {
        static_assert(std::is_base_of_v<IRenderView, T>);
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        if (auto* window = dynamic_cast<WindowView*>(value.get())) {
            window->m_windows = m_windows;
            for (const auto& slot : m_views)
                if (auto* existing = dynamic_cast<WindowView*>(slot.value.get()); existing && existing->window == window->window)
                    throw std::logic_error("Window already has a render view");
        }
        std::uint32_t index = 0;
        while (index < m_views.size() && m_views[index].value) ++index;
        if (index == m_views.size()) m_views.emplace_back();
        m_views[index].value = std::move(value);
        return {index, m_views[index].generation, m_ownerId};
    }
    bool DestroyView(RenderViewHandle view);
    IRenderView* GetView(RenderViewHandle view) noexcept;
    const IRenderView* GetView(RenderViewHandle view) const noexcept;
    template <typename T> T* GetView(RenderViewHandle view) noexcept { return dynamic_cast<T*>(GetView(view)); }
    template <typename T> const T* GetView(RenderViewHandle view) const noexcept { return dynamic_cast<const T*>(GetView(view)); }

    RenderGraph Compile(std::span<const OutputViewHandle> views);
    RenderGraph Compile(std::initializer_list<OutputViewHandle> views) {
        return Compile(std::span<const OutputViewHandle>(views.begin(), views.size()));
    }

private:
    struct Slot { std::uint32_t generation{1}; std::unique_ptr<IRenderView> value; };
    WindowManager* m_windows{};
    std::uint64_t m_ownerId{};
    std::vector<Slot> m_views;
    std::unique_ptr<IRenderDevice> m_device;
    std::unique_ptr<RenderMeshManager> m_renderMeshManager;
};

} // namespace Lighthouse
