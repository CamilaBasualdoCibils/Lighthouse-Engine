#include <Lighthouse/Engine/Rendering/SwapchainTechniques.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

namespace Lighthouse {

void AcquireSwapchainImageTechnique::BuildGraph(TechniqueBuildContext& ctx) {
    auto* view = ctx.GetParentViewAs<WindowView>();
    if (!view || !view->swapchain || !view->GetWindow())
        throw std::logic_error("Swapchain acquire technique requires a WindowView with a swapchain");
    const auto* window = view->GetWindow();
    ImageDesc desc;
    desc.width = window->FramebufferWidth();
    desc.height = window->FramebufferHeight();
    desc.format = ImageFormat::RGBA8;
    desc.usage = ImageUsage::ColorAttachment;
    const auto image = ctx.graph.Import<ImportedRenderGraphImage>("window image", desc, {});
    const auto& pass = ctx.graph.AddPass<PassData>("acquire window image",
        [image](RenderGraph::Builder& builder, PassData& data) {
            data.image = builder.Write(image);
        },
        [swapchain = view->swapchain](const PassData& data, FrameGraphPassResources& resources,
                                      IRenderDevice& device) {
            resources.get<ImportedRenderGraphImage>(data.image).view = device.AcquireSwapchainImage(swapchain);
        });
    m_passData = pass;
}

void PresentSwapchainImageTechnique::BuildGraph(TechniqueBuildContext& ctx) {
    auto* view = ctx.GetParentViewAs<WindowView>();
    if (!view || !view->swapchain)
        throw std::logic_error("Swapchain present technique requires a WindowView with a swapchain");
    const auto image = m_sourceImage(ctx);
    if (image == InvalidGraphImage)
        throw std::logic_error("Swapchain present technique requires a built source image");
    const auto& pass = ctx.graph.AddPass<PassData>("present window image",
        [image](RenderGraph::Builder& builder, PassData& data) {
            data.image = builder.Read(image);
            builder.SetSideEffect();
        },
        [swapchain = view->swapchain](const PassData&, FrameGraphPassResources&, IRenderDevice& device) {
            device.Present(swapchain);
        });
    m_passData = pass;
}

} // namespace Lighthouse
