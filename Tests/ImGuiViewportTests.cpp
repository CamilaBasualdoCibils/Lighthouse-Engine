#if defined(LIGHTHOUSE_IMGUI) && defined(__linux__)
#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#include <gtest/gtest.h>
#include <imgui.h>
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xutil.h>

using namespace Lighthouse;
namespace
{
class ImGuiViewportPixels : public testing::TestWithParam<bool>
{
};
TEST_P(ImGuiViewportPixels, SecondaryViewportContainsRenderedPixels)
{
  if (!std::getenv("DISPLAY"))
    GTEST_SKIP() << "X11 display required";
  WindowManager windows;
  std::unique_ptr<IGraphicsBackend> backend;
  if (GetParam())
    backend = std::make_unique<VulkanBackend>();
  else
    backend = std::make_unique<OpenGLBackend>();
  std::string diagnostic;
  ASSERT_TRUE(backend->Initialize(diagnostic)) << diagnostic;
  RenderGraphComposer renderer(
      backend->CreateDevice(
          {.queues = {{.capabilities =
                           Lighthouse::RenderQueueCapability::Graphics |
                           Lighthouse::RenderQueueCapability::Compute |
                           Lighthouse::RenderQueueCapability::Transfer,
                       .minCount = 1}}}),
      &windows);
  ImGuiContext* context{};
  ImGuiViewport* viewport{};
  ImVec2 windowSize{320, 200};
  renderer.CreateView<ImGuiView>(
      [&]
      {
        context = ImGui::GetCurrentContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::SetNextWindowPos({80, 80}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
        ImGui::Begin("Backend ImGui pixel regression", nullptr,
                     ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("Visible text and geometry");
        auto p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled({p.x + 10, p.y + 10},
                                                  {p.x + 110, p.y + 70},
                                                  IM_COL32(255, 0, 255, 255));
        viewport = ImGui::GetWindowViewport();
        ImGui::End();
      });
  renderer.Compile();
  for (int frame = 0; frame < 8; ++frame)
  {
    windows.PollEvents();
    renderer.Render();
    renderer.GetRenderDevice()->WaitIdle();
    glfwWaitEventsTimeout(.02);
  }
  ASSERT_NE(context, nullptr);
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(viewport->PlatformHandle, nullptr);
  ImGui::SetCurrentContext(context);
  EXPECT_NE(viewport, ImGui::GetMainViewport());
  EXPECT_TRUE(ImGui::GetIO().ConfigViewportsNoAutoMerge);
  EXPECT_TRUE(ImGui::GetIO().ConfigViewportsNoDefaultParent);
  EXPECT_TRUE(ImGui::GetIO().BackendFlags &
              ImGuiBackendFlags_RendererHasViewports);
  ASSERT_NE(viewport->DrawData, nullptr);
  EXPECT_GT(viewport->DrawData->TotalVtxCount, 0);
  std::cout << "ImGui draw size " << viewport->DrawData->DisplaySize.x << "x"
            << viewport->DrawData->DisplaySize.y << " scale "
            << viewport->DrawData->FramebufferScale.x << ","
            << viewport->DrawData->FramebufferScale.y << "\n";
  ImGui::SetCurrentContext(nullptr);
  auto* native = static_cast<GLFWwindow*>(viewport->PlatformHandle);
  ASSERT_TRUE(glfwGetWindowAttrib(native, GLFW_VISIBLE));
  EXPECT_EQ(glfwGetWindowAttrib(native, GLFW_CLIENT_API),
            GetParam() ? GLFW_NO_API : GLFW_OPENGL_API);
  if (GetParam())
    ASSERT_NE(viewport->RendererUserData, nullptr);
  windowSize = {420, 280};
  glfwSetWindowSize(native, 420, 280);
  for (int frame = 0; frame < 4; ++frame)
  {
    windows.PollEvents();
    renderer.Render();
    renderer.GetRenderDevice()->WaitIdle();
    glfwWaitEventsTimeout(.02);
  }
  int width{}, height{};
  glfwGetFramebufferSize(native, &width, &height);
  EXPECT_EQ(width, 420);
  EXPECT_EQ(height, 280);
  auto* display = glfwGetX11Display();
  XSync(display, False);
  auto* pixels = XGetImage(display, glfwGetX11Window(native), 0, 0, width,
                           height, AllPlanes, ZPixmap);
  ASSERT_NE(pixels, nullptr);
  unsigned colored = 0;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
      auto value = XGetPixel(pixels, x, y);
      if ((value & pixels->red_mask) && !(value & pixels->green_mask) &&
          (value & pixels->blue_mask))
        ++colored;
    }
  XDestroyImage(pixels);
  EXPECT_GT(colored, 3000u)
      << "ImGui platform window has no rendered magenta rectangle";
}
TEST_P(ImGuiViewportPixels, HostOverlaysSceneAndOnlyMergesIntoOptedInWindow)
{
  if (!std::getenv("DISPLAY")) GTEST_SKIP() << "X11 display required";
  WindowManager windows;
  std::unique_ptr<IGraphicsBackend> backend;
  if (GetParam()) backend = std::make_unique<VulkanBackend>();
  else backend = std::make_unique<OpenGLBackend>();
  std::string diagnostic;
  ASSERT_TRUE(backend->Initialize(diagnostic)) << diagnostic;
  RenderGraphComposer composer(backend->CreateDevice({.queues = {{.capabilities =
      RenderQueueCapability::Graphics | RenderQueueCapability::Compute |
      RenderQueueCapability::Transfer, .minCount = 1}}}), &windows);
  const auto root = composer.CreateWindowView({.title = "ImGui host",
      .width = 480, .height = 360, .ImGuiHost = true});
  const auto other = composer.CreateWindowView({.title = "Unregistered window",
      .width = 200, .height = 160});
  auto* host = composer.GetView<WindowView>(root);
  auto* unrelated = composer.GetView<WindowView>(other);
  auto* native = static_cast<GLFWwindow*>(host->GetWindow()->NativeHandle());
  auto* otherNative = static_cast<GLFWwindow*>(unrelated->GetWindow()->NativeHandle());
  glfwSetWindowPos(native, 40, 40);
  glfwSetWindowPos(otherNative, 600, 40);
  EXPECT_THROW(composer.CreateWindowView({.ImGuiHost = true}), std::logic_error);
  auto* display = glfwGetX11Display();
  const auto desktop = DefaultRootWindow(display);
  XWindowAttributes desktopAttributes{};
  ASSERT_TRUE(XGetWindowAttributes(display, desktop, &desktopAttributes));
  struct RestoreRootEvents {
    Display* display;
    ::Window root;
    long mask;
    ~RestoreRootEvents() { XSelectInput(display, root, mask); }
  } restoreRootEvents{display, desktop, desktopAttributes.your_event_mask};
  XSelectInput(display, desktop, desktopAttributes.your_event_mask | SubstructureNotifyMask);

  for (auto* view : {host, unrelated}) {
    composer.AddLambdaTechnique([view](TechniqueBuildContext& context) {
      struct Data { RenderGraphResource image; };
      context.graph.AddPass<Data>("Scene clear",
          [view](RenderGraph::Builder& builder, Data& data) {
            data.image = builder.Write(view->GetAcquiredImageAsRGResource());
          }, [view](const Data& data, FrameGraphPassResources& resources,
                    RenderGraphExecutePassContext& execution) {
            const auto& image = resources.get<ImportedWindowView>(data.image);
            const auto size = view->GetWindow()->GetFramebufferSize();
            execution.commandList->BeginRendering({.colorAttachment = image.currentImageView,
                .clearColor = true, .clearColorValue = {0.f, 1.f, 0.f},
                .width = size.width, .height = size.height});
            execution.commandList->EndRendering();
          });
    });
  }
  ImGuiContext* context{};
  ImGuiViewport* merged{};
  ImGuiViewport* detached{};
  const auto ui = composer.CreateView<ImGuiView>([&] {
    context = ImGui::GetCurrentContext();
    ImGui::GetIO().IniFilename = nullptr;
    const auto pos = ImGui::GetMainViewport()->Pos;
    ImGui::SetNextWindowPos({pos.x + 20, pos.y + 30}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({240, 180}, ImGuiCond_Always);
    ImGui::Begin("Merged panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
    auto p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled({p.x + 10, p.y + 10},
        {p.x + 110, p.y + 70}, IM_COL32(255, 0, 255, 255));
    merged = ImGui::GetWindowViewport();
    ImGui::End();
    int x{}, y{};
    glfwGetWindowPos(otherNative, &x, &y);
    ImGui::SetNextWindowPos({float(x + 10), float(y + 10)}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({160, 120}, ImGuiCond_Always);
    ImGui::Begin("Detached panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
    auto q = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled({q.x + 10, q.y + 10},
        {q.x + 110, q.y + 70}, IM_COL32(255, 0, 255, 255));
    detached = ImGui::GetWindowViewport();
    ImGui::End();
  });
  composer.Compile();
  auto drainRaiseRequests = [&] {
    if (!detached || !detached->PlatformHandle) return 0;
    struct Request { Atom type; ::Window window; } request{
        XInternAtom(display, "_NET_RESTACK_WINDOW", False), glfwGetX11Window(static_cast<GLFWwindow*>(detached->PlatformHandle))};
    XSync(display, False);
    XEvent event{};
    int count = 0;
    while (XCheckIfEvent(display, &event,
        [](Display*, XEvent* event, XPointer argument) -> Bool {
          const auto& request = *reinterpret_cast<Request*>(argument);
          return event->type == ClientMessage &&
              event->xclient.message_type == request.type &&
              event->xclient.window == request.window;
        }, reinterpret_cast<XPointer>(&request))) ++count;
    return count;
  };
  int raiseRequests = 0;
  auto renderFrames = [&] {
    for (int frame = 0; frame < 8; ++frame) {
      windows.PollEvents(); composer.Render();
      composer.GetRenderDevice()->WaitIdle();
      raiseRequests += drainRaiseRequests();
      glfwWaitEventsTimeout(.02);
    }
  };
  renderFrames();
  ASSERT_NE(context, nullptr);
  ImGui::SetCurrentContext(context);
  EXPECT_FALSE(ImGui::GetIO().ConfigViewportsNoAutoMerge);
  EXPECT_EQ(ImGui::GetMainViewport()->PlatformHandle, native);
  EXPECT_EQ(merged, ImGui::GetMainViewport());
  ASSERT_NE(detached, nullptr);
  EXPECT_NE(detached, ImGui::GetMainViewport());
  EXPECT_NE(detached->PlatformHandle, otherNative);
  EXPECT_TRUE(ImGui::GetIO().ConfigViewportsNoDefaultParent);
  auto* detachedNative = static_cast<GLFWwindow*>(detached->PlatformHandle);
  ::Window transientParent{};
  EXPECT_FALSE(XGetTransientForHint(glfwGetX11Display(), glfwGetX11Window(detachedNative),
                                    &transientParent));
  EXPECT_EQ(glfwGetWindowAttrib(detachedNative, GLFW_FLOATING), GLFW_FALSE);
  EXPECT_EQ(raiseRequests, 1) << "Detached panel must be raised on first show";

  ImGui::SetCurrentContext(nullptr);
  auto checkPixels = [&](GLFWwindow* target, bool scene) {
    int width{}, height{};
    glfwGetFramebufferSize(target, &width, &height);
    auto* display = glfwGetX11Display();
    XSync(display, False);
    auto* pixels = XGetImage(display, glfwGetX11Window(target), 0, 0,
        width, height, AllPlanes, ZPixmap);
    ASSERT_NE(pixels, nullptr);
    unsigned magenta{}, green{};
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
      const auto value = XGetPixel(pixels, x, y);
      if ((value & pixels->red_mask) && !(value & pixels->green_mask) &&
          (value & pixels->blue_mask)) ++magenta;
      if (!(value & pixels->red_mask) && (value & pixels->green_mask) &&
          !(value & pixels->blue_mask)) ++green;
    }
    XDestroyImage(pixels);
    EXPECT_GT(magenta, 3000u) << "ImGui geometry missing";
    if (scene) EXPECT_GT(green, 3000u) << "Overlay erased scene color";
  };
  checkPixels(native, true);
  checkPixels(static_cast<GLFWwindow*>(detached->PlatformHandle), false);
  raiseRequests = 0;
  glfwSetWindowSize(native, 520, 400);
  renderFrames();
  EXPECT_EQ(raiseRequests, 0) << "Existing panels must not be repeatedly raised";
  checkPixels(native, true);
  EXPECT_EQ(host->GetWindow()->GetFramebufferSize().width, 520u);
  EXPECT_EQ(host->GetWindow()->GetFramebufferSize().height, 400u);
  composer.GetRenderGraph().Clear();
  ASSERT_TRUE(composer.DestroyView(ui));
  EXPECT_NE(windows.GetWindow(host->window), nullptr);
  EXPECT_EQ(glfwGetWindowAttrib(native, GLFW_VISIBLE), GLFW_TRUE);
}

TEST_P(ImGuiViewportPixels, UiOnlyHostRemovalRestoresExternalOnlyMode)
{
  if (!std::getenv("DISPLAY")) GTEST_SKIP() << "X11 display required";
  WindowManager windows;
  std::unique_ptr<IGraphicsBackend> backend;
  if (GetParam()) backend = std::make_unique<VulkanBackend>();
  else backend = std::make_unique<OpenGLBackend>();
  std::string diagnostic;
  ASSERT_TRUE(backend->Initialize(diagnostic)) << diagnostic;
  RenderGraphComposer composer(backend->CreateDevice({.queues = {{.capabilities =
      RenderQueueCapability::Graphics | RenderQueueCapability::Compute |
      RenderQueueCapability::Transfer, .minCount = 1}}}), &windows);
  const auto root = composer.CreateWindowView({.title = "UI-only host",
      .width = 400, .height = 300, .ImGuiHost = true});
  const auto window = composer.GetView<WindowView>(root)->window;
  // Preserve coverage of the user's existing descriptor constructor/demo flags.
  composer.CreateView<ImGuiView>(ImGuiViewDesc{.showImguiDemo = true, .showImplotDemo = true});
  composer.Compile();
  for (int frame = 0; frame < 4; ++frame) {
    windows.PollEvents(); composer.Render();
  }
  ASSERT_TRUE(composer.DestroyView(root));
  EXPECT_EQ(windows.GetWindow(window), nullptr);
  composer.Compile();
  for (int frame = 0; frame < 4; ++frame) {
    windows.PollEvents(); composer.Render();
  }
}

INSTANTIATE_TEST_SUITE_P(Backends, ImGuiViewportPixels,
                         testing::Values(false, true), [](const auto& info)
                         { return info.param ? "Vulkan14" : "OpenGL"; });
} // namespace
#endif
