#include "Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp"
#include "Lighthouse/Engine/Rendering/IRenderCommandList.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include <Lighthouse/Engine/Rendering/ImGuiTechnique.hpp>

#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <implot.h>

#include <stdexcept>

namespace Lighthouse {
namespace {
SwapchainHandle WindowSwapchain(TechniqueBuildContext &ctx) {
  auto *view = ctx.GetParentViewAs<WindowView>();
  if (!view || !view->swapchain)
    throw std::logic_error("ImGui technique requires a window image");
  return view->swapchain;
}
} // namespace

ImGuiSession::~ImGuiSession() {
  if (!m_context)
    return;
  ImGui::SetCurrentContext(m_context);
  ImPlot::SetCurrentContext(m_plotContext);
  if (m_rendererInitialized)
    m_backend->ShutdownImGui();
  if (m_platformInitialized)
    ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext(m_plotContext);
  ImGui::DestroyContext(m_context);
}

void ImGuiSession::Begin(IRenderDevice &device, SwapchainHandle swapchain) {
  auto *backend = dynamic_cast<IImGuiDevice *>(&device);
  if (!backend)
    throw std::logic_error("The active render backend does not support ImGui");
  if (!m_context) {
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    m_plotContext = ImPlot::CreateContext();
    m_backend = backend;
    auto *nativeWindow = static_cast<GLFWwindow *>(m_window.NativeHandle());
    const bool platformReady =
        backend->PlatformBackend() == ImGuiPlatformBackend::Vulkan
            ? ImGui_ImplGlfw_InitForVulkan(nativeWindow, true)
            : ImGui_ImplGlfw_InitForOther(nativeWindow, true);
    if (!platformReady)
      throw std::runtime_error("Failed to initialize ImGui GLFW input");
    m_platformInitialized = true;
    backend->InitializeImGui(swapchain);
    m_rendererInitialized = true;
  }
  if (m_backend != backend)
    throw std::logic_error("ImGui session changed render devices");
  ImGui::SetCurrentContext(m_context);
  ImPlot::SetCurrentContext(m_plotContext);
  backend->NewImGuiFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiSession::End(IRenderDevice &device, SwapchainHandle swapchain,
                       ImageViewHandle image) {
  if (!m_context || m_backend != dynamic_cast<IImGuiDevice *>(&device))
    throw std::logic_error("ImGui frame was not started on this render device");
  ImGui::SetCurrentContext(m_context);
  ImPlot::SetCurrentContext(m_plotContext);
  ImGui::Render();
  m_backend->RenderImGui(swapchain, image, ImGui::GetDrawData());
}

void ImGuiTechnique::BuildGraph(TechniqueBuildContext &ctx) {
  const auto swapchain = WindowSwapchain(ctx);
  const auto &pass = ctx.graph.AddPass<PassData>(
      "render ImGui",
      [](RenderGraph::Builder &builder, PassData &data) {
        data.image = builder.Create<RenderGraphImage>(
            "ImguiDraw", RenderGraphImage::Desc{.width = 1280,
                                                .height = 720,
                                                .format = ImageFormat::RGBA8});
        data.image = builder.Write(data.image);
      },
      [session = m_session, swapchain, draw = m_draw,
       ctx](const PassData &data, FrameGraphPassResources &resources,
            IRenderCommandList &device) {
        auto active = session.lock();
        if (!active)
          throw std::logic_error(
              "ImGui session was destroyed before rendering");
        const auto &image = resources.get<RenderGraphImage>(data.image);
        active->Begin(ctx.device, swapchain);
        draw();
        active->End(ctx.device, swapchain, image.viewHandle);
      });
  m_passData = pass;
}

} // namespace Lighthouse
