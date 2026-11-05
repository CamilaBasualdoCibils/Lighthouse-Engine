#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <gtest/gtest.h>
#ifdef LIGHTHOUSE_TEST_VULKAN
#include <Lighthouse/Vulkan/VulkanBackend.hpp>
#endif

using namespace Lighthouse;
class ImGuiViewTests : public testing::Test {
    void TearDown() override { spdlog::drop("RenderGraphSimpleAllocator"); }
};

TEST_F(ImGuiViewTests, IndependentViewportsRestoreContextAndSurviveEngineWindowCreation) {
    WindowManager windows;
    windows.Initialize(); // Display failures must fail this integration test.
    ASSERT_EQ(glfwGetPlatform(), GLFW_PLATFORM_X11);
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(backend.CreateDevice({}), &windows);
    Scene scene("Test");
    const auto camera = CameraSource{ExplicitCamera{}};
    auto createEngineWindow = [&] {
        const auto handle = renderer.CreateView<WindowView>(RenderWindowDesc{
            .title = "Engine", .width = 160, .height = 120, .visible = false},
            RenderViewDesc{.scene = scene.Handle(), .camera = camera});
        auto* view = renderer.GetView<WindowView>(handle);
        view->AddLambdaTechnique([view](TechniqueBuildContext& ctx) {
            struct Data { RenderGraphResource image; };
            ctx.graph.AddPass<Data>("clear", [view](RenderGraph::Builder& b, Data& d) {
                d.image = b.Write(view->GetAcquiredImageAsRGResource());
            }, [view](const Data& d, FrameGraphPassResources& r, RenderGraphExecutePassContext& ctx) {
                const auto& image = r.get<ImportedWindowView>(d.image);
                const auto size = view->GetWindow()->GetFramebufferSize();
                ctx.commandList->BeginRendering({.colorAttachment = image.currentImageView,
                    .clearColor = true, .clearColorValue = {0.f, 1.f, 0.f},
                    .width = size.width, .height = size.height});
                ctx.commandList->EndRendering();
            });
        });
        return handle;
    };
    const auto engine = createEngineWindow();
    auto* engineWindow = renderer.GetView<WindowView>(engine)->GetWindow();
    const auto engineNative = engineWindow->NativeHandle();
    ImGuiContext* uiContext{};
    int callbacks = 0;
    bool profilerOpen = true;
    const auto ui = renderer.CreateView<ImGuiView>([&] {
        uiContext = ImGui::GetCurrentContext();
        ImGui::GetIO().IniFilename = nullptr;
        ++callbacks;
        ImGui::SetNextWindowPos({40, 40}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({240, 160}, ImGuiCond_Once);
        ImGui::Begin("Inspector");
        ImGui::TextUnformatted("Independent Inspector");
        ImGui::Button("Button");
        ImGui::End();
        ImGui::SetNextWindowPos({400, 40}, ImGuiCond_Once);
        ImGui::SetNextWindowSize({240, 160}, ImGuiCond_Once);
        if (profilerOpen) {
            ImGui::Begin("Profiler", &profilerOpen);
            ImGui::TextUnformatted("Independent Profiler");
            ImGui::End();
        }
    });
    renderer.Compile();
    for (int frame = 0; frame < 4; ++frame) {
        windows.PollEvents();
        renderer.Render();
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
        EXPECT_EQ(glfwGetCurrentContext(), nullptr);
        EXPECT_NE(eglGetCurrentContext(), EGL_NO_CONTEXT);
        EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }
    EXPECT_EQ(callbacks, 4);
    ImGui::SetCurrentContext(uiContext);
    const auto& io = ImGui::GetIO();
    EXPECT_TRUE(io.ConfigFlags & ImGuiConfigFlags_DockingEnable);
    EXPECT_TRUE(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable);
    EXPECT_TRUE(io.ConfigViewportsNoAutoMerge);
    EXPECT_FALSE(io.ConfigViewportsNoDecoration);
    const auto& platform = ImGui::GetPlatformIO();
    ASSERT_GE(platform.Viewports.Size, 3);
    EXPECT_FALSE(glfwGetWindowAttrib(static_cast<GLFWwindow*>(platform.Viewports[0]->PlatformHandle), GLFW_VISIBLE));
    for (int i = 1; i < platform.Viewports.Size; ++i) {
        auto* native = static_cast<GLFWwindow*>(platform.Viewports[i]->PlatformHandle);
        ASSERT_NE(native, nullptr);
        EXPECT_NE(static_cast<void*>(native), engineNative);
        EXPECT_TRUE(glfwGetWindowAttrib(native, GLFW_VISIBLE));
    }
    auto* inspector = ImGui::FindWindowByName("Inspector");
    auto* profiler = ImGui::FindWindowByName("Profiler");
    ASSERT_NE(inspector, nullptr);
    ASSERT_NE(profiler, nullptr);
    EXPECT_NE(inspector->Viewport, ImGui::GetMainViewport());
    EXPECT_NE(profiler->Viewport, ImGui::GetMainViewport());
    EXPECT_NE(inspector->Viewport, profiler->Viewport);
    auto* profilerNative = static_cast<GLFWwindow*>(profiler->Viewport->PlatformHandle);
    ImGui::SetCurrentContext(nullptr);
    glfwSetWindowPos(profilerNative, 450, 180);
    glfwSetWindowSize(profilerNative, 320, 210);
    for (int frame = 0; frame < 4; ++frame) {
        glfwWaitEventsTimeout(0.02);
        windows.PollEvents();
        renderer.Render();
    }
    ImGui::SetCurrentContext(uiContext);
    EXPECT_NEAR(profiler->Pos.x, 450.f, 2.f);
    EXPECT_NEAR(profiler->Pos.y, 180.f, 2.f);
    EXPECT_NEAR(profiler->Size.x, 320.f, 2.f);
    EXPECT_NEAR(profiler->Size.y, 210.f, 2.f);
    // Dock only these ImGui panels together; no engine window is a dock host.
    const auto dock = ImHashStr("test dock");
    ImGui::DockBuilderAddNode(dock);
    ImGui::DockBuilderSetNodeSize(dock, {400, 240});
    ImGui::DockBuilderDockWindow("Inspector", dock);
    ImGui::DockBuilderDockWindow("Profiler", dock);
    ImGui::DockBuilderFinish(dock);
    ImGui::SetCurrentContext(nullptr);
    // Engine creation resets GLFW hints. Later ImGui viewports must still have
    // real shared OpenGL contexts rather than inheriting GLFW_NO_API.
    const auto secondEngine = createEngineWindow();
    renderer.Compile();
    for (int frame = 0; frame < 3; ++frame) { windows.PollEvents(); renderer.Render(); }
    ImGui::SetCurrentContext(uiContext);
    EXPECT_NE(inspector->DockNode, nullptr);
    EXPECT_EQ(inspector->DockNode, profiler->DockNode);
    ImGui::SetCurrentContext(nullptr);
    // Exercise the actual native-close callback outside an ImGui frame.
    // Docking may have destroyed the former standalone Profiler viewport;
    // use the current shared docking host's native window instead.
    ImGui::SetCurrentContext(uiContext);
    auto* dockNative = static_cast<GLFWwindow*>(profiler->Viewport->PlatformHandle);
    ImGui::SetCurrentContext(nullptr);
    const auto dockClose = glfwSetWindowCloseCallback(dockNative, nullptr);
    ASSERT_NE(dockClose, nullptr);
    glfwSetWindowCloseCallback(dockNative, dockClose);
    dockClose(dockNative);
    windows.PollEvents();
    renderer.Render();
    EXPECT_NE(renderer.GetView(engine), nullptr);
    EXPECT_TRUE(renderer.DestroyView(ui));
    EXPECT_NE(windows.GetWindow(engineWindow->Handle()), nullptr);
    EXPECT_NE(renderer.GetView<WindowView>(secondEngine)->GetWindow(), nullptr);
    renderer.Compile();
    renderer.Render();
    // Repeated initialization/teardown must release host/viewport resources.
    for (int iteration = 0; iteration < 3; ++iteration) {
        const auto temporary = renderer.CreateView<ImGuiView>([] {
            ImGui::GetIO().IniFilename = nullptr;
            ImGui::Begin("Temporary"); ImGui::TextUnformatted("Test"); ImGui::End();
        });
        renderer.Compile();
        renderer.Render();
        renderer.Render();
        EXPECT_TRUE(renderer.DestroyView(temporary));
        renderer.Compile();
    }
    EXPECT_EQ(glGetError(), GL_NO_ERROR);
}

TEST_F(ImGuiViewTests, UiOnlyAndCallbackExceptionRestoreContexts) {
    WindowManager windows;
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(backend.CreateDevice({}), &windows);
    bool fail = true;
    const auto ui = renderer.CreateView<ImGuiView>([&] {
        ImGui::GetIO().IniFilename = nullptr;
        if (fail) throw std::runtime_error("UI callback failed");
        ImGui::Begin("Recovered"); ImGui::TextUnformatted("Recovered"); ImGui::End();
    });
    renderer.Compile();
    try {
        renderer.Render();
        FAIL() << "Expected the UI callback to throw";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "UI callback failed");
    }
    EXPECT_EQ(glfwGetCurrentContext(), nullptr);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
    fail = false;
    EXPECT_NO_THROW(renderer.Render());
    EXPECT_NO_THROW(renderer.Render());
    EXPECT_TRUE(renderer.DestroyView(ui));
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}

#ifdef LIGHTHOUSE_TEST_VULKAN
TEST_F(ImGuiViewTests, VulkanReportsExplicitUnsupportedBackendWithoutCreatingWindows) {
    WindowManager windows;
    VulkanBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    std::unique_ptr<IRenderDevice> device;
    try {
        device = backend.CreateDevice({});
    } catch (const std::runtime_error& error) {
        GTEST_SKIP() << "Vulkan instance unavailable: " << error.what();
    }
    Renderer renderer(std::move(device), &windows);
    renderer.CreateView<ImGuiView>([] {});
    renderer.Compile();
    try {
        renderer.Render();
        FAIL() << "Vulkan ImGuiView must report unsupported";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "ImGuiView is not supported by the Vulkan backend yet");
    }
    EXPECT_FALSE(windows.IsAvailable());
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}
#endif
