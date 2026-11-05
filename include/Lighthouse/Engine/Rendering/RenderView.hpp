#pragma once

#include "Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <spdlog/logger.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>


#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>
#include <Lighthouse/Engine/Rendering/RenderTechnique.hpp>

namespace Lighthouse {

class WindowManager;
class IRenderDevice;
struct ViewBuildContext {
    RenderGraph& graph;
    IRenderDevice& device;
};


class IRenderView {
public:
    explicit IRenderView();
    virtual ~IRenderView() = default;
    IRenderView(const IRenderView&) = delete;
    IRenderView& operator=(const IRenderView&) = delete;
    virtual void BuildGraph(ViewBuildContext& ctx);

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

protected:
    void BuildTechniques(ViewBuildContext& ctx);

private:
    struct Slot {
        std::uint32_t generation{1};
        std::unique_ptr<IRenderTechnique> value;
    };
    Slot* FindSlot(RenderTechniqueHandle handle) noexcept;
    const Slot* FindSlot(RenderTechniqueHandle handle) const noexcept;
    std::uint64_t m_ownerId;
    std::vector<Slot> m_techniques;
};

class IOutputView : public IRenderView {
public:
    using IRenderView::IRenderView;

    virtual void Begin(Renderer* renderer)=0;
    virtual void End(Renderer* renderer)=0;
};

class WindowView final : public IOutputView {
public:
    WindowHandle window;
    SwapchainConfig presentation;
    SwapchainHandle swapchain;
    Window* GetWindow() const noexcept;
    void BuildGraph(ViewBuildContext& ctx) override;

    void Begin(Renderer *renderer) override;

    void End(Renderer *renderer) override;

    std::optional<AcquiredSwapchainImage> GetAcquiredImage() const noexcept { return m_acquiredImage; }
    RenderGraphResource GetAcquiredImageAsRGResource() const noexcept { return m_acquiredImageAsRGResource.value_or(kInvalidGraphResource); }
  private:
    friend class Renderer;
    WindowView(WindowHandle window, SwapchainConfig config)
        : IOutputView(), window(window), presentation(config) {}
    WindowManager* m_windows{};
    std::optional<AcquiredSwapchainImage> m_acquiredImage;
    std::optional<RenderGraphResource> m_acquiredImageAsRGResource;
    std::shared_ptr<spdlog::logger> m_logger = (spdlog::get("WindowView") ? spdlog::get("WindowView") : spdlog::stdout_color_mt("WindowView"));
};
class ExportView final : public IOutputView {
public:
  void Begin(Renderer *renderer) override {

  }

  void End(Renderer *renderer) override {

  }

    ExportView(ImageViewHandle imageView, ImportedRenderGraphImage::Desc description)
        : IOutputView(), imageView(imageView), description(description) {}
    ImageViewHandle imageView;
    ImportedRenderGraphImage::Desc description;
    RenderGraphResource GetExportImage() const noexcept { return m_exportImage; }
    void BuildGraph(ViewBuildContext& ctx) override;
private:
    RenderGraphResource m_exportImage{kInvalidGraphResource};
};
class RenderTextureView final : public IRenderView {
public:
    using IRenderView::IRenderView;
    void BuildGraph(ViewBuildContext& ctx) override;
};

template <typename T> T* TechniqueBuildContext::GetParentViewAs() noexcept {
    return dynamic_cast<T*>(&view);
}
template <typename T> const T* TechniqueBuildContext::GetParentViewAs() const noexcept {
    return dynamic_cast<const T*>(&view);
}
template <typename T> const T& TechniqueBuildContext::GetTechnique(TechniqueRef<T> ref) const {
    const auto* technique = view.GetTechnique(ref);
    if (!technique) throw std::logic_error("Technique reference is invalid or belongs to another view");
    return *technique;
}

} // namespace Lighthouse
