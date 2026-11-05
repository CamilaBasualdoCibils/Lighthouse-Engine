#pragma once

#include "Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp"
#include "Lighthouse/Engine/Rendering/Graph/SimpleRenderGraphAllocator.hpp"
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderMesh.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Rendering/RenderTechnique.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

namespace Lighthouse {

class IRenderDevice;
class RenderMeshManager;

// Renderer-facing window options. Native surface setup comes from the device.
struct RenderWindowDesc {
  std::string title;
  std::uint32_t width{1280};
  std::uint32_t height{720};
  WindowMode mode{WindowMode::Windowed};
  bool resizable{true};
  bool visible{true};
  bool decorated{true};
};

class Renderer {
public:
  explicit Renderer(std::unique_ptr<IRenderDevice> device,
                    WindowManager *windows = nullptr);
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  WindowManager* GetWindowManager() noexcept { return m_windows; }
  IRenderDevice *GetRenderDevice() noexcept { return m_device.get(); }
  const IRenderDevice *GetRenderDevice() const noexcept {
    return m_device.get();
  }

  RenderMeshHandle CreateRenderMesh(const RenderMesh &description);
  RenderMeshHandle CreateRenderMesh(const RenderMeshBuilder &builder) {
    return CreateRenderMesh(builder.Build());
  }
  bool DestroyRenderMesh(RenderMeshHandle mesh);
  const RenderMesh *GetRenderMesh(RenderMeshHandle mesh) const noexcept;
  bool IsValid(RenderMeshHandle mesh) const noexcept;
  RenderMeshUploadStatus
  GetRenderMeshUploadStatus(RenderMeshHandle mesh) const noexcept;

  template <typename T, typename... Args>
  RenderViewHandle CreateView(Args &&...args) {
    static_assert(std::is_base_of_v<IRenderView, T>);
    if constexpr (std::is_same_v<T, WindowView>) {
      return CreateWindowView(std::forward<Args>(args)...);
    } else {
      return AddView(std::make_unique<T>(std::forward<Args>(args)...));
    }
  }
  // Creates and owns the native window along with its render view.
  RenderViewHandle CreateWindowView(const RenderWindowDesc& window = {},
                                    SwapchainConfig presentation = {});
  bool DestroyView(RenderViewHandle view);
  IRenderView *GetView(RenderViewHandle view) noexcept;
  const IRenderView *GetView(RenderViewHandle view) const noexcept;
  template <typename T> T *GetView(RenderViewHandle view) noexcept {
    return dynamic_cast<T *>(GetView(view));
  }
  template <typename T> const T *GetView(RenderViewHandle view) const noexcept {
    return dynamic_cast<const T *>(GetView(view));
  }

    template <typename T, typename... Args> TechniqueRef<T> AddTechnique(Args&&... args) {
        static_assert(std::is_base_of_v<IRenderTechnique, T>);
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        std::uint32_t index = 0;
        while (index < m_techniques.size() && m_techniques[index].value) ++index;
        if (index == m_techniques.size()) m_techniques.emplace_back();
        auto& slot = m_techniques[index];
        slot.value = std::move(value);
        return {{index, slot.generation, m_ownerId}};
    }
   template <typename Build>
    TechniqueRef<LambdaTechnique> AddLambdaTechnique(Build&& build) {
        return AddTechnique<LambdaTechnique>(
            std::forward<Build>(build));
    }
    template <typename T> T* GetTechnique(TechniqueRef<T> ref) noexcept {
        return dynamic_cast<T*>(GetTechnique(ref.handle));
    }
    template <typename T> const T* GetTechnique(TechniqueRef<T> ref) const noexcept {
        return dynamic_cast<const T*>(GetTechnique(ref.handle));
    }
    IRenderTechnique* GetTechnique(RenderTechniqueHandle handle) noexcept;
    const IRenderTechnique* GetTechnique(RenderTechniqueHandle handle) const noexcept;
    bool RemoveTechnique(RenderTechniqueHandle handle);
    template <typename T> bool RemoveTechnique(TechniqueRef<T> ref) { return RemoveTechnique(ref.handle); }
    std::vector<RenderTechniqueHandle> Techniques() const;
    std::uint64_t OwnerId() const noexcept { return m_ownerId; }


  RenderGraph &GetRenderGraph() noexcept { return m_renderGraph; }
  void Compile();
  void Render();

private:
    struct TechniqueSlot {
        std::uint32_t generation{1};
        std::unique_ptr<IRenderTechnique> value;
    };
    TechniqueSlot* FindSlot(RenderTechniqueHandle handle) noexcept;
    const TechniqueSlot* FindSlot(RenderTechniqueHandle handle) const noexcept;
    std::vector<TechniqueSlot> m_techniques;
  RenderViewHandle AddView(std::unique_ptr<IRenderView> value);
  struct Slot {
    std::uint32_t generation{1};
    std::unique_ptr<IRenderView> value;
  };
  WindowManager *m_windows{};
  std::uint64_t m_ownerId{};
  std::vector<Slot> m_views;
  std::unique_ptr<IRenderDevice> m_device;
  std::unique_ptr<RenderMeshManager> m_renderMeshManager;

  RenderGraph m_renderGraph;
  SimpleRenderGraphAllocator m_renderGraphAllocator;
};

template <typename T> const T& TechniqueBuildContext::GetTechnique(TechniqueRef<T> ref) const {
    const auto* technique = renderer.GetTechnique(ref);
    if (!technique) throw std::logic_error("Technique reference is invalid or belongs to another renderer");
    return *technique;
}

} // namespace Lighthouse
