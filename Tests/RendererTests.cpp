#include <Lighthouse/Engine/Renderer.hpp>
#include <Lighthouse/Engine/EngineRuntime.hpp>
#include <Lighthouse/Engine/Scene.hpp>

#include <gtest/gtest.h>

namespace {

class TestTechnique final : public Engine::IRenderTechnique {
public:
    void Build(Engine::RenderGraph&, const Engine::RenderView&, const Engine::RenderScene&) override {}
};

Engine::RenderViewDesc MakeView(Engine::SceneHandle scene, Engine::RenderTechniqueHandle technique = {}) {
    Engine::RenderViewDesc desc;
    desc.scene = scene;
    desc.camera = Engine::ExplicitCamera{};
    desc.technique = technique;
    return desc;
}

TEST(RendererTests, MultipleViewsCanReferenceTheSameScene) {
    Engine::Renderer renderer;
    Engine::Scene world{"World"};
    const auto scene = world.Handle();
    auto first = renderer.CreateView(MakeView(scene));
    auto secondDesc = MakeView(scene);
    secondDesc.settings.clearColor = {0.2f, 0.3f, 0.4f};
    auto second = renderer.CreateView(secondDesc);

    ASSERT_NE(first, second);
    ASSERT_NE(renderer.GetView(first), nullptr);
    EXPECT_EQ(renderer.GetView(first)->GetScene(), renderer.GetView(second)->GetScene());
    EXPECT_NE(renderer.GetView(first)->GetSettings().clearColor.x, renderer.GetView(second)->GetSettings().clearColor.x);
}

TEST(RendererTests, EntityCameraSourceBecomesInvalidWhenSceneIsDestroyed) {
    Engine::CameraSource camera;
    {
        Engine::Scene scene{"CameraOwner"};
        auto entity = scene.CreateEntity("Camera");
        camera = Engine::CameraSource{entity};
        EXPECT_TRUE(camera.IsValid());
    }
    EXPECT_FALSE(camera.IsValid());
}

TEST(RendererTests, SceneHandleInvalidatesWhenSceneIsDestroyed) {
    Engine::SceneHandle handle;
    {
        Engine::Scene scene{"Temporary"};
        handle = scene.Handle();
        EXPECT_TRUE(handle.IsValid());
    }
    EXPECT_FALSE(handle.IsValid());
}

TEST(RendererTests, SessionsBecomeInvalidWhenTheirSceneIsDestroyed) {
    Engine::Renderer renderer;
    Engine::RenderSessionHandle session;
    {
        Engine::Scene scene{"Temporary"};
        const auto view = renderer.CreateView(MakeView(scene.Handle()));
        session = renderer.CreateSession({.view = view, .output = Engine::RenderOutput::Offscreen({0, 1})});
        EXPECT_TRUE(renderer.IsSessionValid(session));
    }
    EXPECT_FALSE(renderer.IsSessionValid(session));
}

TEST(RendererTests, TechniquesCanBeRegisteredReassignedAndRemoved) {
    Engine::Renderer renderer;
    Engine::Scene scene{"Techniques"};
    auto firstTechnique = renderer.RegisterTechnique(std::make_unique<TestTechnique>());
    auto secondTechnique = renderer.RegisterTechnique(std::make_unique<TestTechnique>());
    auto viewHandle = renderer.CreateView(MakeView(scene.Handle(), firstTechnique));
    auto* view = renderer.GetView(viewHandle);
    ASSERT_NE(view, nullptr);
    ASSERT_NE(renderer.GetTechnique(firstTechnique), nullptr);

    view->SetTechnique(secondTechnique);
    EXPECT_EQ(view->GetTechnique(), secondTechnique);
    EXPECT_TRUE(renderer.UnregisterTechnique(firstTechnique));
    EXPECT_EQ(renderer.GetTechnique(firstTechnique), nullptr);
    EXPECT_FALSE(renderer.UnregisterTechnique(firstTechnique));
}

TEST(RendererTests, MultipleSessionsCanShareAViewAndBecomeInvalidWhenItIsDestroyed) {
    Engine::Renderer renderer;
    Engine::Scene scene{"SessionScene"};
    const auto view = renderer.CreateView(MakeView(scene.Handle()));
    Engine::RenderSessionDesc desc{.view = view, .output = Engine::RenderOutput::Offscreen({0, 1})};
    const auto first = renderer.CreateSession(desc);
    const auto second = renderer.CreateSession(desc);

    EXPECT_TRUE(renderer.IsSessionValid(first));
    EXPECT_TRUE(renderer.IsSessionValid(second));
    EXPECT_TRUE(renderer.DestroyView(view));
    EXPECT_FALSE(renderer.IsSessionValid(first));
    EXPECT_FALSE(renderer.IsSessionValid(second));
}

TEST(RendererTests, DestroyedWindowsAndTechniquesInvalidateSessionsSafely) {
    Engine::WindowManager windows;
    Engine::Renderer renderer{&windows};
    Engine::Scene scene{"OutputScene"};
    const auto technique = renderer.RegisterTechnique(std::make_unique<TestTechnique>());
    const auto view = renderer.CreateView(MakeView(scene.Handle(), technique));
    const auto staleWindow = Engine::WindowHandle{0, 1};
    const auto session = renderer.CreateSession({.view = view, .output = Engine::RenderOutput::ForWindow(staleWindow)});
    EXPECT_FALSE(renderer.IsSessionValid(session));
    renderer.GetView(view)->SetTechnique({});
    const auto offscreen = renderer.CreateSession({.view = view, .output = Engine::RenderOutput::Offscreen({0, 1})});
    EXPECT_TRUE(renderer.IsSessionValid(offscreen));
    renderer.GetView(view)->SetTechnique(technique);
    renderer.UnregisterTechnique(technique);
    EXPECT_FALSE(renderer.IsSessionValid(offscreen));
}

TEST(EngineRuntimeTests, InitializesAndShutsDownWithoutWindowsOrGraphicsBackend) {
    Engine::BackendRegistry registry;
    Engine::EngineRuntime runtime;
    Engine::Config config;

    EXPECT_TRUE(runtime.Initialize(config, registry));
    EXPECT_FALSE(runtime.HasGraphicsBackend());
    EXPECT_NO_THROW(runtime.GetWindowManager().PollEvents());
    EXPECT_NO_THROW(runtime.Shutdown());
}

} // namespace
