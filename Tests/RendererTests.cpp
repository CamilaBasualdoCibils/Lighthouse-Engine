#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>

#include <gtest/gtest.h>

namespace {
using namespace Lighthouse;

struct TestTechnique final : IRenderTechnique {
    using Outputs = int;
    explicit TestTechnique(std::vector<int>& order, int id) : order(order), id(id) {}
    void Build(TechniqueBuildContext& ctx) override {
        order.push_back(id);
        ctx.SetOutputs(id);
    }
    std::vector<int>& order;
    int id;
};
struct Consumer final : IRenderTechnique {
    using Outputs = int;
    Consumer(TechniqueRef<TestTechnique> source, int& result) : source(source), result(result) {}
    void Build(TechniqueBuildContext& ctx) override {
        result = ctx.GetOutputs(source);
        ctx.SetOutputs(result + 1);
    }
    TechniqueRef<TestTechnique> source;
    int& result;
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
    view.GetTechnique(source)->Build(first);
    TechniqueBuildContext second(graph, view, outputs, consumer.handle);
    view.GetTechnique(consumer)->Build(second);
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
TEST(RendererTests, EngineRuntimeStartsWithoutGraphicsBackend) {
    BackendRegistry registry;
    EngineRuntime runtime;
    Config config;
    EXPECT_TRUE(runtime.Initialize(config, registry));
    EXPECT_NO_THROW(runtime.Shutdown());
}
} // namespace
