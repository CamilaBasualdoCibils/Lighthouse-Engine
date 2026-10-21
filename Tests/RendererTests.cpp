#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>

#include <gtest/gtest.h>

namespace {
using namespace Lighthouse;

struct TestTechnique final : IRenderTechnique {
    using Outputs = int;
    explicit TestTechnique(std::vector<int>& order, int id) : order(order), id(id) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        order.push_back(id);
        ctx.SetOutputs(id);
    }
    std::vector<int>& order;
    int id;
};
struct Consumer final : IRenderTechnique {
    using Outputs = int;
    Consumer(TechniqueRef<TestTechnique> source, int& result) : source(source), result(result) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        result = ctx.GetOutputs(source);
        ctx.SetOutputs(result + 1);
    }
    TechniqueRef<TestTechnique> source;
    int& result;
};
struct ImagePassTechnique final : IRenderTechnique {
    using Outputs = GraphImageHandle;
    explicit ImagePassTechnique(int& builds, int& executions)
        : builds(builds), executions(executions) {}
    struct PassData { GraphImageHandle image{}; };
    void BuildGraph(TechniqueBuildContext& ctx) override {
        ++builds;
        const std::string passName = "test image";
        const auto& data = ctx.graph.AddPass<PassData>(passName,
            [](RenderGraph::Builder& builder, PassData& data) {
                ImageDesc desc;
                desc.format = ImageFormat::RGBA8;
                const std::string resourceName = "test image resource";
                data.image = builder.Create<RenderGraphTexture>(resourceName, desc);
                builder.SetSideEffect();
            },
            [counter = &executions](const PassData&, FrameGraphPassResources&, IRenderDevice&) {
                ++*counter;
            });
        ctx.SetFinalImage(data.image);
        ctx.SetOutputs(data.image);
    }
    int& builds;
    int& executions;
};
struct TypedImageConsumerTechnique final : IRenderTechnique {
    explicit TypedImageConsumerTechnique(TechniqueRef<ImagePassTechnique> source) : source(source) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        ctx.SetFinalImage(ctx.GetOutputs(source));
    }
    TechniqueRef<ImagePassTechnique> source;
};
struct ViewConsumerTechnique final : IRenderTechnique {
    ViewConsumerTechnique(RenderViewHandle source, int& builds) : source(source), builds(builds) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        ++builds;
        ctx.SetFinalImage(ctx.GetViewOutput(source));
    }
    RenderViewHandle source;
    int& builds;
};
struct CountingTechnique final : IRenderTechnique {
    explicit CountingTechnique(int& count) : count(count) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        ++count;
        ctx.SetFinalImage(InvalidGraphImage);
    }
    int& count;
};
struct ExportWriterTechnique final : IRenderTechnique {
    explicit ExportWriterTechnique(ImageViewHandle expected, int& writes) : expected(expected), writes(writes) {}
    struct PassData { GraphImageHandle image; };
    void BuildGraph(TechniqueBuildContext& ctx) override {
        const auto& pass = ctx.graph.AddPass<PassData>("write export image",
            [target = ctx.TargetImage()](RenderGraph::Builder& builder, PassData& data) {
                data.image = builder.Write(target);
            },
            [expected = expected, writes = &writes](const PassData& data,
                FrameGraphPassResources& resources, IRenderDevice&) {
                EXPECT_EQ(resources.get<ImportedRenderGraphImage>(data.image).view, expected);
                ++*writes;
            });
        ctx.SetFinalImage(pass.image);
    }
    ImageViewHandle expected;
    int& writes;
};
class CountingOutputView final : public IOutputView {
public:
    CountingOutputView(RenderViewDesc desc, int& count) : IOutputView(std::move(desc)), count(count) {}
    GraphImageHandle BuildGraph(ViewBuildContext& ctx) override {
        ++count;
        return BuildTechniques(ctx);
    }
    int& count;
};
class TestRenderDevice final : public IRenderDevice {
public:
    SwapchainHandle CreateSwapchain(Window&, const SwapchainConfig&) override { return {}; }
    ImageViewHandle AcquireSwapchainImage(SwapchainHandle) override { return {}; }
    bool DestroySwapchain(SwapchainHandle) override { return false; }
    bool IsValid(SwapchainHandle) const noexcept override { return false; }
    ImageHandle CreateImage(const ImageDesc&) override { return {1, 1, DeviceId()}; }
    ImageViewHandle CreateImageView(const ImageViewDesc&) override { return {}; }
    BufferHandle CreateBuffer(const BufferDesc&) override { return {}; }
    std::optional<BufferMetadata> GetBufferDesc(BufferHandle) const noexcept override { return {}; }
    bool IsValid(BufferHandle) const noexcept override { return false; }
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override { return {}; }
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override { return {}; }
    std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override { return {}; }
    void Destroy(ImageHandle) override {}
    void Destroy(ImageViewHandle) override {}
    void Destroy(BufferHandle) override {}
    void Destroy(GraphicsPipelineHandle) override {}
    void Destroy(ShaderModuleHandle) override {}
    RenderCapabilities GetCapabilities() const override { return {}; }
    void Submit(const IRenderCommandList&) override {}
    void Present(SwapchainHandle) override {}
};
RenderViewDesc MakeView(SceneHandle scene) {
    RenderViewDesc desc;
    desc.scene = scene;
    desc.camera = ExplicitCamera{};
    return desc;
}
TEST(RendererTests, ViewsHaveGenerationalHandlesAndIndependentSettings) {
    Renderer renderer;
    Scene scene{"World"};
    auto first = renderer.CreateView<RenderTextureView>(MakeView(scene.Handle()));
    auto second = renderer.CreateView<RenderTextureView>(MakeView(scene.Handle()));
    ASSERT_NE(renderer.GetView(first), nullptr);
    renderer.GetView(first)->SetVisibleLayers(3);
    EXPECT_NE(renderer.GetView(first)->GetVisibleLayers(), renderer.GetView(second)->GetVisibleLayers());
    EXPECT_TRUE(renderer.DestroyView(first));
    EXPECT_EQ(renderer.GetView(first), nullptr);
    EXPECT_FALSE(renderer.DestroyView(first));
    EXPECT_NE(first, renderer.CreateView<RenderTextureView>());
}
TEST(RendererTests, SceneAndCameraLifetimeIsChecked) {
    CameraSource camera;
    SceneHandle handle;
    {
        Scene scene{"Temporary"};
        handle = scene.Handle();
        camera = CameraSource{scene.CreateEntity("Camera")};
        EXPECT_TRUE(handle.IsValid());
        EXPECT_TRUE(camera.IsValid());
    }
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(camera.IsValid());
}
TEST(RendererTests, TechniqueReferencesInvalidateAndDependenciesSort) {
    RenderTextureView view;
    std::vector<int> order;
    int result{};
    auto consumer = view.AddTechnique<Consumer>(TechniqueRef<TestTechnique>{}, result);
    auto source = view.AddTechnique<TestTechnique>(order, 7);
    view.GetTechnique(consumer)->source = source;
    view.SetDependencies(consumer, source);
    auto sorted = view.TechniqueOrder();
    ASSERT_EQ(sorted.size(), 2);
    EXPECT_EQ(sorted[0], source.handle);
    EXPECT_EQ(sorted[1], consumer.handle);
    RenderGraph graph;
    std::unordered_map<std::uint32_t, std::any> outputs;
    TechniqueBuildContext first(graph, view, outputs, source.handle);
    view.GetTechnique(source)->BuildGraph(first);
    TechniqueBuildContext second(graph, view, outputs, consumer.handle);
    view.GetTechnique(consumer)->BuildGraph(second);
    EXPECT_EQ(result, 7);
    EXPECT_TRUE(view.RemoveTechnique(source));
    EXPECT_EQ(view.GetTechnique(source), nullptr);
    EXPECT_THROW(view.TechniqueOrder(), std::logic_error);
    auto replacement = view.AddTechnique<TestTechnique>(order, 8);
    EXPECT_NE(source.handle, replacement.handle);
}
TEST(RendererTests, CyclesAndForeignDependenciesAreRejected) {
    RenderTextureView first, second;
    std::vector<int> order;
    auto a = first.AddTechnique<TestTechnique>(order, 1);
    auto b = first.AddTechnique<TestTechnique>(order, 2);
    first.SetDependencies(a, b);
    first.SetDependencies(b, a);
    EXPECT_THROW(first.TechniqueOrder(), std::logic_error);
    auto foreign = second.AddTechnique<TestTechnique>(order, 3);
    first.SetDependencies(b, foreign);
    EXPECT_THROW(first.TechniqueOrder(), std::logic_error);
}
TEST(RendererTests, CompileAcceptsOutputRootsAndRejectsTransientRoots) {
    Renderer renderer;
    Scene scene{"Roots"};
    auto desc = MakeView(scene.Handle());
    auto first = renderer.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
    auto second = renderer.CreateView<ExportView>(ImageHandle{2, 1}, ImageViewHandle{2, 1}, ImageDesc{}, desc);
    auto transient = renderer.CreateView<RenderTextureView>(desc);
    EXPECT_NO_THROW(renderer.Compile({first, second}));
    EXPECT_THROW(renderer.Compile({transient}), std::logic_error);
    EXPECT_TRUE(renderer.DestroyView(first));
    EXPECT_THROW(renderer.Compile({first}), std::logic_error);
}
TEST(RendererTests, CompileDelegatesGraphBuildingToViewAndTechnique) {
    Renderer renderer;
    Scene scene{"Delegation"};
    int viewBuilds{}, techniqueBuilds{};
    auto handle = renderer.CreateView<CountingOutputView>(MakeView(scene.Handle()), viewBuilds);
    auto* view = renderer.GetView(handle);
    view->SetFinalOutput(view->AddTechnique<CountingTechnique>(techniqueBuilds));
    auto graph = renderer.Compile({handle});
    EXPECT_EQ(viewBuilds, 1);
    EXPECT_EQ(techniqueBuilds, 1);
    TestRenderDevice device;
    EXPECT_NO_THROW(graph.Execute(device));
}
TEST(RendererTests, ExportViewImportsItsTargetForTechniqueWrites) {
    Renderer renderer;
    Scene scene{"Export"};
    const ImageViewHandle imageView{3, 1};
    auto handle = renderer.CreateView<ExportView>(ImageHandle{2, 1}, imageView,
        ImageDesc{}, MakeView(scene.Handle()));
    int writes{};
    auto* view = renderer.GetView(handle);
    view->SetFinalOutput(view->AddTechnique<ExportWriterTechnique>(imageView, writes));
    auto graph = renderer.Compile({handle});
    TestRenderDevice device;
    graph.Execute(device);
    EXPECT_EQ(writes, 1);
}
TEST(RendererTests, CompileBuildsSharedTransientDependencyOnceAndReturnedGraphExecutes) {
    int producerBuilds{}, consumerBuilds{}, executions{};
    RenderGraph graph;
    {
        Renderer renderer;
        Scene scene{"Dependencies"};
        auto desc = MakeView(scene.Handle());
        auto producer = renderer.CreateView<RenderTextureView>(desc);
        auto* producerView = renderer.GetView(producer);
        auto image = producerView->AddTechnique<ImagePassTechnique>(producerBuilds, executions);
        producerView->SetFinalOutput(image);

        auto consumer = renderer.CreateView<RenderTextureView>(desc);
        auto* consumerView = renderer.GetView(consumer);
        consumerView->AddViewDependency(producer);
        auto use = consumerView->AddTechnique<ViewConsumerTechnique>(producer, consumerBuilds);
        consumerView->SetFinalOutput(use);

        auto first = renderer.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
        auto second = renderer.CreateView<ExportView>(ImageHandle{2, 1}, ImageViewHandle{2, 1}, ImageDesc{}, desc);
        renderer.GetView(first)->AddViewDependency(consumer);
        renderer.GetView(second)->AddViewDependency(consumer);
        graph = renderer.Compile({first, second});
        EXPECT_EQ(producerBuilds, 1);
        EXPECT_EQ(consumerBuilds, 1);
    }
    TestRenderDevice device;
    EXPECT_NO_THROW(graph.Execute(device));
    EXPECT_NO_THROW(graph.Execute(device));
    EXPECT_EQ(executions, 2);
}
TEST(RendererTests, CompileResolvesTypedTechniqueOutputsBeforeConsumers) {
    Renderer renderer;
    Scene scene{"Typed outputs"};
    auto desc = MakeView(scene.Handle());
    auto source = renderer.CreateView<RenderTextureView>(desc);
    auto* view = renderer.GetView(source);
    auto consumer = view->AddTechnique<TypedImageConsumerTechnique>(TechniqueRef<ImagePassTechnique>{});
    int builds{}, executions{};
    auto producer = view->AddTechnique<ImagePassTechnique>(builds, executions);
    view->GetTechnique(consumer)->source = producer;
    view->SetDependencies(consumer, producer);
    view->SetFinalOutput(consumer);
    auto output = renderer.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
    renderer.GetView(output)->AddViewDependency(source);
    auto graph = renderer.Compile({output});
    EXPECT_EQ(builds, 1);
    TestRenderDevice device;
    graph.Execute(device);
    EXPECT_EQ(executions, 1);
}
TEST(RendererTests, CompileRejectsCrossViewCyclesAndInvalidDependencies) {
    Renderer renderer;
    Scene scene{"Cycles"};
    auto desc = MakeView(scene.Handle());
    auto first = renderer.CreateView<RenderTextureView>(desc);
    auto second = renderer.CreateView<RenderTextureView>(desc);
    auto output = renderer.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
    renderer.GetView(output)->AddViewDependency(first);
    renderer.GetView(first)->AddViewDependency(second);
    renderer.GetView(second)->AddViewDependency(first);
    EXPECT_THROW(renderer.Compile({output}), std::logic_error);
}
TEST(RendererTests, CompileRejectsForeignViewDependency) {
    Renderer first, second;
    Scene scene{"Foreign"};
    auto desc = MakeView(scene.Handle());
    auto source = first.CreateView<RenderTextureView>(desc);
    auto output = second.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
    second.GetView(output)->AddViewDependency(source);
    EXPECT_THROW(second.Compile({output}), std::logic_error);
}
TEST(RendererTests, CompileRejectsMissingViewDependency) {
    Renderer renderer;
    Scene scene{"Missing"};
    auto desc = MakeView(scene.Handle());
    auto output = renderer.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
    renderer.GetView(output)->AddViewDependency(RenderViewHandle{999, 1});
    EXPECT_THROW(renderer.Compile({output}), std::logic_error);
}
TEST(RendererTests, EngineRuntimeStartsWithoutGraphicsBackend) {
    BackendRegistry registry;
    EngineRuntime runtime;
    Config config;
    EXPECT_TRUE(runtime.Initialize(config, registry));
    EXPECT_NO_THROW(runtime.Shutdown());
}
} // namespace
