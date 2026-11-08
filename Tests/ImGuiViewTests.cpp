#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#include <Lighthouse/Engine/Rendering/ImguiSimpleObject.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Tools/ISimpleTool.hpp>
#include <Lighthouse/Engine/Tools/RendererTool.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <EGL/egl.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <gtest/gtest.h>
#ifdef LIGHTHOUSE_TEST_VULKAN
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
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
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
    auto createEngineWindow = [&] {
        const auto handle = renderer.CreateView<WindowView>(RenderWindowDesc{
            .title = "Engine", .width = 160, .height = 120, .visible = false});
        auto* view = renderer.GetView<WindowView>(handle);
        renderer.AddLambdaTechnique([view](TechniqueBuildContext& ctx) {
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
    const auto callerContext = eglGetCurrentContext();
    for (int frame = 0; frame < 4; ++frame) {
        windows.PollEvents();
        renderer.Render();
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
        EXPECT_EQ(glfwGetCurrentContext(), nullptr);
        EXPECT_EQ(eglGetCurrentContext(), callerContext);
        if (callerContext != EGL_NO_CONTEXT)
          EXPECT_EQ(glGetError(), GL_NO_ERROR);
    }
    EXPECT_EQ(callbacks, 4);
    ImGui::SetCurrentContext(uiContext);
    const auto& io = ImGui::GetIO();
    EXPECT_TRUE(io.ConfigFlags & ImGuiConfigFlags_DockingEnable);
    EXPECT_TRUE(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable);
    EXPECT_TRUE(io.ConfigViewportsNoAutoMerge);
    EXPECT_TRUE(io.ConfigViewportsNoDecoration);
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
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
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
TEST_F(ImGuiViewTests, VulkanGraphFrames) {
    WindowManager windows;
    VulkanBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    std::unique_ptr<IRenderDevice> device;
    try
    {
      device = backend.CreateDevice(
          {.queues = {
               {.capabilities = Lighthouse::RenderQueueCapability::Graphics |
                                Lighthouse::RenderQueueCapability::Compute |
                                Lighthouse::RenderQueueCapability::Transfer,
                .minCount = 1}}});
    }
    catch (const std::runtime_error& error) { GTEST_SKIP() << error.what(); }
    Renderer renderer(std::move(device), &windows);
    int calls = 0;
    renderer.CreateView<ImGuiView>([&] {
        ++calls;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::Begin("Vulkan graph UI"); ImGui::TextUnformatted("Graph pass"); ImGui::End();
    });
    renderer.Compile();
    EXPECT_EQ(calls, 0);
    for (int frame=0; frame<3; ++frame) { windows.PollEvents(); renderer.Render(); }
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
}
#endif

namespace {
class RecordingSimpleObject final : public ImguiSimpleObject {
public:
    explicit RecordingSimpleObject(std::vector<int>& events) : events(events) {}
    void Draw() override {
        context = ImGui::GetCurrentContext();
        EXPECT_TRUE(context->WithinFrameScope);
        ImGui::GetIO().IniFilename = nullptr;
        events.push_back(1);
    }
    std::vector<int>& events;
    ImGuiContext* context{};
};
class RecordingGraphObject final : public ImguiGraphObject {
public:
    RecordingGraphObject(std::vector<int>& events, bool& fail) : events(events), fail(fail) {}
    RenderGraphResource BuildGraph(ImguiBuildContext& context, RenderGraphResource completionFlag) override {
        ++builds;
        struct Data { RenderGraphResource completion; };
        return context.AddPass<Data>("Custom ImGui object",
            [completionFlag](RenderGraph::Builder& builder, Data& data) { data.completion = builder.Write(completionFlag); },
            [this](const Data&, FrameGraphPassResources&, RenderGraphExecutePassContext&) {
                EXPECT_TRUE(ImGui::GetCurrentContext()->WithinFrameScope);
                events.push_back(2);
                if (fail) throw std::runtime_error("Graph object failed");
            }).completion;
    }
    int builds{};
    std::vector<int>& events;
    bool& fail;
};
class RecordingSimpleTool final : public ISimpleTool {
public:
    RecordingSimpleTool(EngineRuntime&, std::vector<int>& events) : events(events) {}
    std::string_view Name() const noexcept override { return "Simple test tool"; }
    void Draw() override { events.push_back(3); ImGui::TextUnformatted("Simple tool pass"); }
    std::vector<int>& events;
};
class RecordingGraphTool final : public ITool {
public:
    RecordingGraphTool(EngineRuntime&, std::vector<int>& events) : events(events) {}
    std::string_view Name() const noexcept override { return "Custom test tool"; }
    RenderGraphResource BuildGraph(ImguiBuildContext& context, RenderGraphResource completionFlag) override {
        struct Data { RenderGraphResource completion; };
        return context.AddPass<Data>(Name(),
            [completionFlag](RenderGraph::Builder& builder, Data& data) { data.completion = builder.Write(completionFlag); },
            [this](const Data&, FrameGraphPassResources&, RenderGraphExecutePassContext&) { events.push_back(4); }).completion;
    }
    std::vector<int>& events;
};
}
TEST_F(ImGuiViewTests, GraphObjectOrderRecompileAndFailureRecovery) {
    WindowManager windows;
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
    std::vector<int> events;
    bool fail = false;
    auto simple = std::make_shared<RecordingSimpleObject>(events);
    auto custom = std::make_shared<RecordingGraphObject>(events, fail);
    auto handle = renderer.CreateView<ImGuiView>();
    auto* view = renderer.GetView<ImGuiView>(handle);
    view->AddObject(simple); view->AddObject(custom);
    EXPECT_THROW(view->AddObject(std::shared_ptr<ImguiGraphObject>{}), std::invalid_argument);
    renderer.Compile();
    EXPECT_TRUE(events.empty()); EXPECT_EQ(custom->builds, 1);
    const auto dot = renderer.GetRenderGraph().Dot();
    EXPECT_NE(dot.find("Completion"), std::string::npos);
    EXPECT_NE(dot.find("ImGui NewFrame"), std::string::npos);
    EXPECT_NE(dot.find("ImGui Render"), std::string::npos);
    renderer.Render();
    EXPECT_EQ(events, (std::vector<int>{1, 2}));
    ASSERT_NE(simple->context, nullptr);
    EXPECT_FALSE(simple->context->WithinFrameScope);
    EXPECT_EQ(simple->context->FrameCountRendered, simple->context->FrameCount);
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
    events.clear(); renderer.Compile(); EXPECT_TRUE(events.empty());
    EXPECT_EQ(custom->builds, 2);
    fail = true;
    EXPECT_THROW(renderer.Render(), std::runtime_error);
    EXPECT_EQ(events, (std::vector<int>{1, 2}));
    EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
    EXPECT_EQ(glfwGetCurrentContext(), nullptr);
    fail = false; events.clear();
    EXPECT_NO_THROW(renderer.Render());
    EXPECT_EQ(events, (std::vector<int>{1, 2}));
}
TEST_F(ImGuiViewTests, ToolboxChainsCustomAndSimpleToolPasses) {
    EngineRuntime runtime;
    auto& toolbox = runtime.GetToolBox();
    std::vector<int> events;
    toolbox.AddTool<RecordingSimpleTool>(events);
    toolbox.AddTool<RecordingGraphTool>(events);
    WindowManager windows;
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
    auto handle = renderer.CreateView<ImGuiView>();
    auto* view = renderer.GetView<ImGuiView>(handle);
    view->AddObject(toolbox);
    renderer.Compile(); EXPECT_TRUE(events.empty());
    auto dot = renderer.GetRenderGraph().Dot();
    EXPECT_NE(dot.find("Toolbox Layout"), std::string::npos);
    EXPECT_NE(dot.find("Simple test tool"), std::string::npos);
    EXPECT_NE(dot.find("Custom test tool"), std::string::npos);
    renderer.Render();
    EXPECT_EQ(events, (std::vector<int>{3, 4}));
    events.clear(); renderer.Compile(); EXPECT_TRUE(events.empty());
    renderer.Render(); EXPECT_EQ(events, (std::vector<int>{3, 4}));
    // An empty object list still runs frame setup and rendering.
    auto empty = renderer.CreateView<ImGuiView>();
    renderer.Compile(); EXPECT_NO_THROW(renderer.Render());
    renderer.DestroyView(empty); renderer.Compile();
}

TEST_F(ImGuiViewTests, RendererToolUsesItsOwnGraphPassAndNodeEditorContext) {
    EngineRuntime runtime;
    auto& toolbox = runtime.GetToolBox();
    toolbox.AddTool<RendererTool>();
    WindowManager windows;
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
    auto handle = renderer.CreateView<ImGuiView>();
    auto* view = renderer.GetView<ImGuiView>(handle);
    view->AddObject(toolbox);
    std::vector<int> events;
    auto recorder = std::make_shared<RecordingSimpleObject>(events);
    view->AddObject(recorder);
    renderer.Compile();
    EXPECT_NE(renderer.GetRenderGraph().Dot().find("Renderer"), std::string::npos);
    for (int frame = 0; frame < 3; ++frame) renderer.Render();
    ASSERT_NE(recorder->context, nullptr);
    ImGui::SetCurrentContext(recorder->context);
    auto* window = ImGui::FindWindowByName("Renderer");
    ASSERT_NE(window, nullptr);
    EXPECT_NE(window->DockId, 0u);
    ImGui::SetCurrentContext(nullptr);
    // Dropping the compiled snapshot also releases the node editor safely.
    toolbox.Clear();
    renderer.Compile();
    EXPECT_NO_THROW(renderer.Render());
}

namespace {
class PostBuildOutput final : public IOutputView {
public:
    explicit PostBuildOutput(std::vector<int>& events) : events(events) {}
    void BuildGraph(ViewBuildContext&) override { events.push_back(1); }
    void PostBuildGraph(ViewBuildContext&) override { events.push_back(3); }
    void Begin(Renderer*) override {}
    void End(Renderer*) override {}
    std::vector<int>& events;
};
class PostBuildProbe final : public ImguiGraphObject {
public:
    PostBuildProbe(std::vector<int>& events, RenderGraphResource& output)
        : events(events), output(output) {}
    RenderGraphResource BuildGraph(ImguiBuildContext& context, RenderGraphResource completion) override {
        events.push_back(4);
        EXPECT_NE(output, kInvalidGraphResource);
        struct Data { RenderGraphResource completion; };
        return context.AddPass<Data>("PostBuild probe",
            [this, completion](RenderGraph::Builder& builder, Data& data) {
                builder.Read(output);
                data.completion = builder.Write(completion);
            }, [](const Data&, FrameGraphPassResources&, RenderGraphExecutePassContext&) {}).completion;
    }
    std::vector<int>& events;
    RenderGraphResource& output;
};
}
TEST_F(ImGuiViewTests, PostBuildRunsAfterAllViewsAndTechniques) {
    WindowManager windows;
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
    std::vector<int> events;
    RenderGraphResource output = kInvalidGraphResource;
    renderer.CreateView<PostBuildOutput>(events);
    auto ui = renderer.CreateView<ImGuiView>();
    renderer.GetView<ImGuiView>(ui)->AddObject(std::make_shared<PostBuildProbe>(events, output));
    renderer.AddLambdaTechnique([&](TechniqueBuildContext& context) {
        events.push_back(2);
        output = context.graph.Import<ImportedRenderGraphBuffer>("Technique output", {}, {});
    });
    renderer.Compile();
    EXPECT_EQ(events, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_NE(renderer.GetRenderGraph().Dot().find("PostBuild probe"), std::string::npos);
    events.clear();
    renderer.Compile();
    EXPECT_EQ(events, (std::vector<int>{1, 2, 3, 4}));
}

TEST_F(ImGuiViewTests, RendererInspectorShowsSceneMetadataWithoutKeepingPassesAlive) {
    EngineRuntime runtime;
    auto& toolbox = runtime.GetToolBox();
    toolbox.AddTool<RendererTool>();
    WindowManager windows;
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic));
    Renderer renderer(
        backend.CreateDevice(
            {.queues = {{.capabilities =
                             Lighthouse::RenderQueueCapability::Graphics |
                             Lighthouse::RenderQueueCapability::Compute |
                             Lighthouse::RenderQueueCapability::Transfer,
                         .minCount = 1}}}),
        &windows);
    int sceneExecutions = 0;
    bool expanded = false;
    renderer.AddLambdaTechnique([&](TechniqueBuildContext& context) {
        struct Data { RenderGraphResource image; };
        context.graph.AddPass<Data>("Visible producer",
            [](RenderGraph::Builder& builder, Data& data) {
                data.image = builder.Create<RenderGraphBuffer>("Scene output", {.size = 16});
            }, [&](const Data&, FrameGraphPassResources&, RenderGraphExecutePassContext&) { ++sceneExecutions; });
        if (expanded) (void)context.graph.Import<ImportedRenderGraphBuffer>("Extra external resource", {}, {});
    });
    auto ui = renderer.CreateView<ImGuiView>();
    renderer.GetView<ImGuiView>(ui)->AddObject(toolbox);
    std::vector<int> events;
    auto recorder = std::make_shared<RecordingSimpleObject>(events);
    renderer.GetView<ImGuiView>(ui)->AddObject(recorder);
    renderer.Compile();
    auto snapshot = renderer.GetRenderGraph().GetInspectionData();
    ASSERT_EQ(snapshot.passes.size(), 1u); ASSERT_EQ(snapshot.resources.size(), 1u);
    EXPECT_EQ(snapshot.passes[0].name, "Visible producer");
    EXPECT_EQ(snapshot.resources[0].name, "Scene output");
    const auto all = renderer.GetRenderGraph().GetInspectionData(true);
    EXPECT_GT(all.passes.size(), snapshot.passes.size());
    bool foundInspector = false;
    for (const auto& pass : all.passes) if (pass.name == "Renderer") {
        foundInspector = true; EXPECT_TRUE(pass.hidden);
        // Only completion is consumed; scene output is inspected as metadata.
        ASSERT_EQ(pass.reads.size(), 1u);
        EXPECT_NE(pass.reads[0], snapshot.resources[0].handle);
    }
    EXPECT_TRUE(foundInspector);
    for (int frame = 0; frame < 3; ++frame) renderer.Render();
    EXPECT_EQ(sceneExecutions, 0); // The unused producer must stay culled.
    ASSERT_NE(recorder->context, nullptr);
    ImGui::SetCurrentContext(recorder->context);
    int vertices = 0;
    for (auto* viewport : ImGui::GetPlatformIO().Viewports)
        if (viewport->DrawData) vertices += viewport->DrawData->TotalVtxCount;
    EXPECT_GT(vertices, 0);
    ImGui::SetCurrentContext(nullptr);
    expanded = true; renderer.Compile();
    EXPECT_EQ(renderer.GetRenderGraph().GetInspectionData().resources.size(), 2u);
    for (int frame = 0; frame < 3; ++frame) EXPECT_NO_THROW(renderer.Render());
    EXPECT_EQ(sceneExecutions, 0);
}
