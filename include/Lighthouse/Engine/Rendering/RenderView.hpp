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


namespace Lighthouse {

class WindowManager;
class IRenderDevice;
struct ViewBuildContext {
    RenderGraph& graph;
    IRenderDevice& device;
};


class IRenderView {
public:
    IRenderView() = default;
    virtual ~IRenderView() = default;
    IRenderView(const IRenderView&) = delete;
    IRenderView& operator=(const IRenderView&) = delete;
    virtual void BuildGraph(ViewBuildContext& ctx);


};

class IOutputView : public IRenderView {
public:
    using IRenderView::IRenderView;

    virtual void BuildGraph(ViewBuildContext& context) override = 0;
    virtual void PostBuildGraph(ViewBuildContext& context) {}

    virtual void Begin(RenderGraphComposer* renderer)=0;
    virtual void End(RenderGraphComposer* renderer)=0;
};

class WindowView final : public IOutputView {
public:
    WindowHandle window;
    SwapchainConfig presentation;
    SurfaceHandle surface;
    SwapchainHandle swapchain;
    SwapchainInfo GetSwapchainInfo() const noexcept
    {
      return m_swapchainInfo;
    }
    Window* GetWindow() const noexcept;
    void BuildGraph(ViewBuildContext& ctx) override;

    void Begin(RenderGraphComposer *renderer) override;

    void End(RenderGraphComposer *renderer) override;

    std::optional<AcquiredSwapchainImage> GetAcquiredImage() const noexcept { return m_acquiredImage; }
    RenderGraphResource GetAcquiredImageAsRGResource() const noexcept { return m_acquiredImageAsRGResource.value_or(kInvalidGraphResource); }
  private:
    friend class RenderGraphComposer;
    WindowView(WindowHandle window, SwapchainConfig config)
        : IOutputView(), window(window), presentation(config) {}
    SwapchainInfo m_swapchainInfo;
    WindowManager* m_windows{};
    std::optional<AcquiredSwapchainImage> m_acquiredImage;
    std::optional<RenderGraphResource> m_acquiredImageAsRGResource;
    std::shared_ptr<spdlog::logger> m_logger = (spdlog::get("WindowView") ? spdlog::get("WindowView") : spdlog::stdout_color_mt("WindowView"));
};
class ExportView final : public IOutputView {
public:
  void Begin(RenderGraphComposer *renderer) override {

  }

  void End(RenderGraphComposer *renderer) override {

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


} // namespace Lighthouse
