#include <Lighthouse/Engine/Engine.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Vulkan/VulkanBackend.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImGuiTechniques.hpp>
#include <imgui.h>
#endif

#include <optional>
#include <stdexcept>

class GreenClearTechnique final : public Lighthouse::IRenderTechnique {
public:
    struct PassData { Lighthouse::GraphImageHandle image{Lighthouse::InvalidGraphImage}; };
    const PassData& GetPassData() const noexcept { return m_passData; }
    void BuildGraph(Lighthouse::TechniqueBuildContext& ctx) override {
        const auto* view = ctx.GetParentViewAs<Lighthouse::WindowView>();
        if (!view || view->GetSwapchainImage() == Lighthouse::InvalidGraphImage)
            throw std::logic_error("Green clear requires a window swapchain image");
        const auto& pass = ctx.graph.AddPass<PassData>("clear window green",
            [target = view->GetSwapchainImage()](Lighthouse::RenderGraph::Builder& builder, PassData& data) {
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
        m_passData = pass;
        ctx.SetFinalImage(m_passData.image);
    }
private:
    PassData m_passData;
};

#ifdef LIGHTHOUSE_IMGUI
class ImGuiDemoTechnique final : public Lighthouse::IRenderTechnique {
public:
    struct PassData { Lighthouse::GraphImageHandle image{Lighthouse::InvalidGraphImage}; };
    explicit ImGuiDemoTechnique(Lighthouse::TechniqueRef<Lighthouse::ImGuiStartTechnique> source)
        : m_source(source) {}
    const PassData& GetPassData() const noexcept { return m_passData; }
    void BuildGraph(Lighthouse::TechniqueBuildContext& ctx) override {
        const auto sourceImage = ctx.GetTechnique(m_source).GetPassData().image;
        const auto& pass = ctx.graph.AddPass<PassData>("ImGui demo",
            [input = sourceImage](Lighthouse::RenderGraph::Builder& builder, PassData& data) {
                data.image = builder.Write(input);
            },
            [](const PassData&, FrameGraphPassResources&, Lighthouse::IRenderDevice&) {
                ImGui::ShowDemoWindow();
            });
        m_passData = pass;
        ctx.SetFinalImage(m_passData.image);
    }
private:
    Lighthouse::TechniqueRef<Lighthouse::ImGuiStartTechnique> m_source;
    PassData m_passData;
};
#endif

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
        const auto clear = view->AddTechnique<GreenClearTechnique>();
#ifdef LIGHTHOUSE_IMGUI
        m_imgui = std::make_shared<Lighthouse::ImGuiSession>(*GetWindowManager().GetWindow(windowHandle));
        const auto begin = view->AddTechnique<Lighthouse::ImGuiStartTechnique>(m_imgui, clear);
        const auto demo = view->AddTechnique<ImGuiDemoTechnique>(begin);
        const auto end = view->AddTechnique<Lighthouse::ImGuiEndTechnique>(m_imgui, demo);
        view->SetDependencies(begin, clear);
        view->SetDependencies(demo, begin);
        view->SetDependencies(end, demo);
        view->SetFinalOutput(end);
#else
        view->SetFinalOutput(clear);
#endif
        m_graph.emplace(GetRenderer().Compile({m_windowView}));
    }

    void OnUpdate(float) override {
        if (auto* device = GetRenderer().GetRenderDevice(); device && m_graph)
            m_graph->Execute(*device);
    }

private:
    Lighthouse::RenderViewHandle m_windowView;
#ifdef LIGHTHOUSE_IMGUI
    std::shared_ptr<Lighthouse::ImGuiSession> m_imgui;
#endif
    std::optional<Lighthouse::RenderGraph> m_graph;
};

int main() {
    Lighthouse::Config config{.name = "Lighthouse Window and Scene Example",
        .graphics = {.selection = Lighthouse::BackendSelection::FirstAvailable}};
    config.AddBackend<Lighthouse::OpenGLBackend>();
    config.AddBackend<Lighthouse::VulkanBackend>();
    return Lighthouse::Run<WindowSceneExample>(config);
}
