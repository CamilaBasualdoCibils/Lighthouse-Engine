#include <Lighthouse/Engine/Engine.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>

#include <optional>
#include <stdexcept>

class GreenClearTechnique final : public Lighthouse::IRenderTechnique {
public:
    void BuildGraph(Lighthouse::TechniqueBuildContext& ctx) override {
        if (ctx.TargetImage() == Lighthouse::InvalidGraphImage)
            throw std::logic_error("Green clear requires an output image");
        struct PassData { Lighthouse::GraphImageHandle image; };
        const auto& pass = ctx.graph.AddPass<PassData>("clear window green",
            [target = ctx.TargetImage()](Lighthouse::RenderGraph::Builder& builder, PassData& data) {
                data.image = builder.Write(target);
            },
            [](const PassData& data, FrameGraphPassResources& resources,
               Lighthouse::IRenderDevice& device) {
                const auto& image = resources.get<Lighthouse::ImportedRenderGraphImage>(data.image);
                const auto& desc = resources.getDescriptor<Lighthouse::ImportedRenderGraphImage>(data.image);
                auto commands = device.CreateCommandList(Lighthouse::QueueType::Graphics);
                Lighthouse::RenderPassDesc pass;
                pass.colorAttachment = image.view;
                pass.width = desc.width;
                pass.height = desc.height;
                pass.clearColor = true;
                pass.clearColorValue = {0.0f, 1.0f, 0.0f};
                commands->BeginRendering(pass);
                commands->EndRendering();
                device.Submit(*commands);
            });
        ctx.SetFinalImage(pass.image);
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
