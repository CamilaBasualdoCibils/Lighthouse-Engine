#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/SwapchainTechniques.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>

#include <gtest/gtest.h>

namespace {
using namespace Lighthouse;

struct TestTechnique final : IRenderTechnique {
    struct PassData { int value{}; };
    explicit TestTechnique(std::vector<int>& order, int id) : order(order), id(id) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        order.push_back(id);
        m_passData.value = id;
    }
    const PassData& GetPassData() const noexcept { return m_passData; }
    std::vector<int>& order;
    int id;
    PassData m_passData;
};
struct Consumer final : IRenderTechnique {
    struct PassData { int value{}; };
    Consumer(TechniqueRef<TestTechnique> source, int& result) : source(source), result(result) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        result = ctx.GetTechnique(source).GetPassData().value;
        m_passData.value = result + 1;
    }
    const PassData& GetPassData() const noexcept { return m_passData; }
    TechniqueRef<TestTechnique> source;
    int& result;
    PassData m_passData;
};
struct ImagePassTechnique final : IRenderTechnique {
    struct PassData { GraphImageHandle image{InvalidGraphImage}; };
    explicit ImagePassTechnique(int& builds, int& executions)
        : builds(builds), executions(executions) {}
    const PassData& GetPassData() const noexcept { return m_passData; }
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
        m_passData = data;
    }
    int& builds;
    int& executions;
    PassData m_passData;
};
struct TypedImageConsumerTechnique final : IRenderTechnique {
    TypedImageConsumerTechnique(TechniqueRef<ImagePassTechnique> source, GraphImageHandle& observed)
        : source(source), observed(observed) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        observed = ctx.GetTechnique(source).GetPassData().image;
    }
    TechniqueRef<ImagePassTechnique> source;
    GraphImageHandle& observed;
};
struct ViewBuildCounterTechnique final : IRenderTechnique {
    explicit ViewBuildCounterTechnique(int& builds) : builds(builds) {}
    void BuildGraph(TechniqueBuildContext&) override { ++builds; }
    int& builds;
};
struct CountingTechnique final : IRenderTechnique {
    explicit CountingTechnique(int& count) : count(count) {}
    void BuildGraph(TechniqueBuildContext& ctx) override {
        ++count;
    }
    int& count;
};
struct ExportWriterTechnique final : IRenderTechnique {
    explicit ExportWriterTechnique(ImageViewHandle expected, int& writes) : expected(expected), writes(writes) {}
    struct PassData { GraphImageHandle image; };
    void BuildGraph(TechniqueBuildContext& ctx) override {
        const auto& pass = ctx.graph.AddPass<PassData>("write export image",
            [target = ctx.GetParentViewAs<ExportView>()->GetExportImage()](RenderGraph::Builder& builder, PassData& data) {
                data.image = builder.Write(target);
            },
            [expected = expected, writes = &writes](const PassData& data,
                FrameGraphPassResources& resources, IRenderDevice&) {
                EXPECT_EQ(resources.get<ImportedRenderGraphImage>(data.image).view, expected);
                ++*writes;
            });
        (void)pass;
    }
    ImageViewHandle expected;
    int& writes;
};
class CountingOutputView final : public IOutputView {
public:
    CountingOutputView(RenderViewDesc desc, int& count) : IOutputView(std::move(desc)), count(count) {}
    void BuildGraph(ViewBuildContext& ctx) override {
        ++count;
        BuildTechniques(ctx);
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
    bool IsValid(DescriptorSetLayoutHandle) const noexcept override { return false; }
    bool IsValid(DescriptorSetHandle) const noexcept override { return false; }
    bool IsValid(PipelineLayoutHandle) const noexcept override { return false; }
    bool IsValid(SamplerHandle) const noexcept override { return false; }
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override { return {}; }
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc&) override { return {}; }
    DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc&) override { return {}; }
    PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc&) override { return {}; }
    SamplerHandle CreateSampler(const SamplerDesc&) override { return {}; }
    void UpdateDescriptorSet(DescriptorSetHandle, std::span<const DescriptorWrite>) override {}
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override { return {}; }
    std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override { return {}; }
    void Destroy(ImageHandle) override {}
    void Destroy(ImageViewHandle) override {}
    void Destroy(BufferHandle) override {}
    void Destroy(GraphicsPipelineHandle) override {}
    void Destroy(ShaderModuleHandle) override {}
    void Destroy(DescriptorSetLayoutHandle) override {}
    void Destroy(DescriptorSetHandle) override {}
    void Destroy(PipelineLayoutHandle) override {}
    void Destroy(SamplerHandle) override {}
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
TEST(RendererTests, TechniquesBuildInInsertionOrderAndReferencesInvalidate) {
    RenderTextureView view;
    std::vector<int> order;
    int result{};
    auto source = view.AddTechnique<TestTechnique>(order, 7);
    auto consumer = view.AddTechnique<Consumer>(source, result);
    RenderGraph graph;
    ViewBuildContext context{graph};
    view.IRenderView::BuildGraph(context);
    EXPECT_EQ(order, std::vector<int>{7});
    EXPECT_EQ(result, 7);
    EXPECT_TRUE(view.RemoveTechnique(source));
    TechniqueBuildContext techniqueContext(graph, view);
    EXPECT_THROW(view.GetTechnique(consumer)->BuildGraph(techniqueContext), std::logic_error);
    auto replacement = view.AddTechnique<TestTechnique>(order, 8);
    EXPECT_NE(source.handle, replacement.handle);
}
TEST(RendererTests, ForeignTechniqueReferencesAreRejected) {
    RenderTextureView first, second;
    std::vector<int> order;
    auto foreign = second.AddTechnique<TestTechnique>(order, 3);
    RenderGraph graph;
    TechniqueBuildContext context(graph, first);
    EXPECT_THROW(context.GetTechnique(foreign), std::logic_error);
}
TEST(RendererTests, SwapchainTechniquesRequireWindowView) {
    RenderTextureView view;
    RenderGraph graph;
    TechniqueBuildContext context(graph, view);
    AcquireSwapchainImageTechnique acquire;
    EXPECT_THROW(acquire.BuildGraph(context), std::logic_error);
    PresentSwapchainImageTechnique present(TechniqueRef<AcquireSwapchainImageTechnique>{});
    EXPECT_THROW(present.BuildGraph(context), std::logic_error);
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
    view->AddTechnique<CountingTechnique>(techniqueBuilds);
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
    view->AddTechnique<ExportWriterTechnique>(imageView, writes);
    auto graph = renderer.Compile({handle});
    TestRenderDevice device;
    graph.Execute(device);
    EXPECT_EQ(writes, 1);
}
TEST(RendererTests, LambdaTechniqueBuildsAndExecutesWithItsPassData) {
    Renderer renderer;
    Scene scene{"Lambda"};
    const ImageViewHandle imageView{4, 1};
    auto handle = renderer.CreateView<ExportView>(ImageHandle{4, 1}, imageView,
        ImageDesc{}, MakeView(scene.Handle()));
    auto* view = renderer.GetView<ExportView>(handle);
    struct Data { GraphImageHandle image{InvalidGraphImage}; };
    int executions{};
    Data& data = view->AddLambdaTechnique<Data>(
        [](Data& pass, TechniqueBuildContext& ctx) {
            pass.image = ctx.Builder().Write(ctx.GetParentViewAs<ExportView>()->GetExportImage());
        },
        [&executions, imageView](const Data& pass, TechniqueExecuteContext& ctx) {
            EXPECT_EQ(ctx.resources.get<ImportedRenderGraphImage>(pass.image).view, imageView);
            ++executions;
        });
    EXPECT_EQ(data.image, InvalidGraphImage);
    auto graph = renderer.Compile({handle});
    EXPECT_NE(data.image, InvalidGraphImage);
    TestRenderDevice device;
    graph.Execute(device);
    EXPECT_EQ(executions, 1);
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
        producerView->AddTechnique<ImagePassTechnique>(producerBuilds, executions);

        auto consumer = renderer.CreateView<RenderTextureView>(desc);
        auto* consumerView = renderer.GetView(consumer);
        consumerView->AddViewDependency(producer);
        consumerView->AddTechnique<ViewBuildCounterTechnique>(consumerBuilds);

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
TEST(RendererTests, CompileResolvesTechniquePassDataBeforeConsumers) {
    Renderer renderer;
    Scene scene{"Typed outputs"};
    auto desc = MakeView(scene.Handle());
    auto source = renderer.CreateView<RenderTextureView>(desc);
    auto* view = renderer.GetView(source);
    int builds{}, executions{};
    auto producer = view->AddTechnique<ImagePassTechnique>(builds, executions);
    GraphImageHandle observed = InvalidGraphImage;
    view->AddTechnique<TypedImageConsumerTechnique>(producer, observed);
    auto output = renderer.CreateView<ExportView>(ImageHandle{1, 1}, ImageViewHandle{1, 1}, ImageDesc{}, desc);
    renderer.GetView(output)->AddViewDependency(source);
    auto graph = renderer.Compile({output});
    EXPECT_EQ(builds, 1);
    EXPECT_NE(observed, InvalidGraphImage);
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
