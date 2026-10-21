#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>

#include <gtest/gtest.h>

namespace {

class TestTechnique final : public Lighthouse::IRenderTechnique {
public:
    void Build(Lighthouse::RenderGraph&, const Lighthouse::RenderView&, const Lighthouse::RenderScene&) override {}
};

Lighthouse::RenderViewDesc MakeView(Lighthouse::SceneHandle scene, Lighthouse::RenderTechniqueHandle technique = {}) {
    Lighthouse::RenderViewDesc desc;
    desc.scene = scene;
    desc.camera = Lighthouse::ExplicitCamera{};
    desc.technique = technique;
    return desc;
}

TEST(RendererTests, MultipleViewsCanReferenceTheSameScene) {
    Lighthouse::Renderer renderer;
    Lighthouse::Scene world{"World"};
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
    Lighthouse::CameraSource camera;
    {
        Lighthouse::Scene scene{"CameraOwner"};
        auto entity = scene.CreateEntity("Camera");
        camera = Lighthouse::CameraSource{entity};
        EXPECT_TRUE(camera.IsValid());
    }
    EXPECT_FALSE(camera.IsValid());
}

TEST(RendererTests, SceneHandleInvalidatesWhenSceneIsDestroyed) {
    Lighthouse::SceneHandle handle;
    {
        Lighthouse::Scene scene{"Temporary"};
        handle = scene.Handle();
        EXPECT_TRUE(handle.IsValid());
    }
    EXPECT_FALSE(handle.IsValid());
}

TEST(RendererTests, SessionsBecomeInvalidWhenTheirSceneIsDestroyed) {
    Lighthouse::Renderer renderer;
    Lighthouse::RenderSessionHandle session;
    {
        Lighthouse::Scene scene{"Temporary"};
        const auto view = renderer.CreateView(MakeView(scene.Handle()));
        session = renderer.CreateSession({.view = view, .output = Lighthouse::RenderOutput::Offscreen({0, 1})});
        EXPECT_TRUE(renderer.IsSessionValid(session));
    }
    EXPECT_FALSE(renderer.IsSessionValid(session));
}

TEST(RendererTests, TechniquesCanBeRegisteredReassignedAndRemoved) {
    Lighthouse::Renderer renderer;
    Lighthouse::Scene scene{"Techniques"};
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
    Lighthouse::Renderer renderer;
    Lighthouse::Scene scene{"SessionScene"};
    const auto view = renderer.CreateView(MakeView(scene.Handle()));
    Lighthouse::RenderSessionDesc desc{.view = view, .output = Lighthouse::RenderOutput::Offscreen({0, 1})};
    const auto first = renderer.CreateSession(desc);
    const auto second = renderer.CreateSession(desc);

    EXPECT_TRUE(renderer.IsSessionValid(first));
    EXPECT_TRUE(renderer.IsSessionValid(second));
    EXPECT_TRUE(renderer.DestroyView(view));
    EXPECT_FALSE(renderer.IsSessionValid(first));
    EXPECT_FALSE(renderer.IsSessionValid(second));
}

TEST(RendererTests, DestroyedWindowsAndTechniquesInvalidateSessionsSafely) {
    Lighthouse::WindowManager windows;
    Lighthouse::Renderer renderer{&windows};
    Lighthouse::Scene scene{"OutputScene"};
    const auto technique = renderer.RegisterTechnique(std::make_unique<TestTechnique>());
    const auto view = renderer.CreateView(MakeView(scene.Handle(), technique));
    const auto staleWindow = Lighthouse::WindowHandle{0, 1};
    const auto session = renderer.CreateSession({.view = view, .output = Lighthouse::RenderOutput::ForWindow(staleWindow)});
    EXPECT_FALSE(renderer.IsSessionValid(session));
    renderer.GetView(view)->SetTechnique({});
    const auto offscreen = renderer.CreateSession({.view = view, .output = Lighthouse::RenderOutput::Offscreen({0, 1})});
    EXPECT_TRUE(renderer.IsSessionValid(offscreen));
    renderer.GetView(view)->SetTechnique(technique);
    renderer.UnregisterTechnique(technique);
    EXPECT_FALSE(renderer.IsSessionValid(offscreen));
}

TEST(EngineRuntimeTests, InitializesAndShutsDownWithoutWindowsOrGraphicsBackend) {
    Lighthouse::BackendRegistry registry;
    Lighthouse::EngineRuntime runtime;
    Lighthouse::Config config;

    EXPECT_TRUE(runtime.Initialize(config, registry));
    EXPECT_FALSE(runtime.HasGraphicsBackend());
    EXPECT_NO_THROW(runtime.GetWindowManager().PollEvents());
    EXPECT_NO_THROW(runtime.Shutdown());
}

} // namespace
