#include <Lighthouse/Engine/Engine.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>

#include <optional>
#include <stdexcept>

class GreenClearTechnique final : public Lighthouse::IRenderTechnique {
public:
    void BuildGraph(Lighthouse::TechniqueBuildContext& ctx) override {
        auto* view = dynamic_cast<Lighthouse::WindowView*>(&ctx.view);
        if (!view || !ctx.windows) throw std::logic_error("Green clear requires a window view");

        const auto swapchain = view->swapchain;
        const auto window = view->window;
        const auto* windows = ctx.windows;
        ctx.graph.AddPass("clear window green",
            [](Lighthouse::RenderGraph::Builder& builder, auto&) {
                builder.SetSideEffect();
            },
            [swapchain, window, windows](const auto&, FrameGraphPassResources&,
                                         Lighthouse::IRenderDevice& device) {
                const auto* target = windows->GetWindow(window);
                if (!target) throw std::logic_error("Window was destroyed before graph execution");
                auto commands = device.CreateCommandList(Lighthouse::QueueType::Graphics);
                Lighthouse::RenderPassDesc pass;
                pass.colorAttachment = device.AcquireSwapchainImage(swapchain);
                pass.width = target->FramebufferWidth();
                pass.height = target->FramebufferHeight();
                pass.clearColor = true;
                pass.clearColorValue = {0.0f, 1.0f, 0.0f};
                commands->BeginRendering(pass);
                commands->EndRendering();
                device.Submit(*commands);
            });
        // This pass writes the acquired presentation image directly.
        ctx.SetFinalImage(Lighthouse::InvalidGraphImage);
    }
};

class WindowSceneExample : public Lighthouse::Application {
public:
    void OnStart() override {
        auto& scene = CreateScene("Main");
        auto camera = scene.CreateEntity("Camera");
        camera.Add<Lighthouse::Transform>();
        camera.Add<Lighthouse::Camera>(Lighthouse::Camera{70.0f});
        scene.SetActiveCamera(camera);

        const auto windowHandle = GetWindowManager().CreateWindow({
            .title = "Lighthouse Window and Scene",
            .width = 960,
            .height = 540,
            .eglSurface = true});
        GetWindowManager().GetWindow(windowHandle)->Show();

        Lighthouse::RenderViewDesc description;
        description.scene = scene.Handle();
        description.camera = Lighthouse::CameraSource{camera};
        m_windowView = GetRenderer().CreateView<Lighthouse::WindowView>(windowHandle, description);
        auto* view = GetRenderer().GetView<Lighthouse::WindowView>(m_windowView);
        view->SetFinalOutput(view->AddTechnique<GreenClearTechnique>());
        m_graph.emplace(GetRenderer().Compile({m_windowView}));
    }

    void OnUpdate(float) override {
        if (auto* device = GetRenderer().GetRenderDevice(); device && m_graph)
            m_graph->Execute(*device);
    }

private:
    Lighthouse::RenderViewHandle m_windowView;
    std::optional<Lighthouse::RenderGraph> m_graph;
};

int main() {
    Lighthouse::Config config{.name = "Lighthouse Window and Scene Example",
        .graphics = {.selection = Lighthouse::BackendSelection::FirstAvailable}};
    config.AddBackend<Lighthouse::OpenGLBackend>();
    return Lighthouse::Run<WindowSceneExample>(config);
}
