#include <Lighthouse/Engine/Engine.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Vulkan/VulkanBackend.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#include <imgui.h>
#endif

class WindowSceneExample : public Lighthouse::Application {
public:
    void OnStart() override {
        using namespace Lighthouse;
        auto& scene = CreateScene("Main");
        auto camera = scene.CreateEntity("Camera");
        camera.Add<Transform>();
        camera.Add<Camera>(Camera{70.0f});
        scene.SetActiveCamera(camera);
        const auto handle = GetRenderer().CreateView<WindowView>(RenderWindowDesc{
            .title = "Lighthouse Window and Scene", .width = 960, .height = 540},
            RenderViewDesc{.scene = scene.Handle(), .camera = CameraSource{camera}});
        auto* view = GetRenderer().GetView<WindowView>(handle);
        GetRenderer().AddLambdaTechnique([view](TechniqueBuildContext& ctx) {
            struct Data { RenderGraphResource image; };
            ctx.graph.AddPass<Data>("clear window green",
                [view](RenderGraph::Builder& builder, Data& data) {
                    data.image = builder.Write(view->GetAcquiredImageAsRGResource());
                },
                [view](const Data& data, FrameGraphPassResources& resources, RenderGraphExecutePassContext& ctx) {
                    const auto& image = resources.get<ImportedWindowView>(data.image);
                    const auto size = view->GetWindow()->GetFramebufferSize();
                    ctx.commandList->BeginRendering({.colorAttachment = image.currentImageView,
                        .clearColor = true, .clearColorValue = {0.f, 1.f, 0.f},
                        .width = size.width, .height = size.height});
                    ctx.commandList->EndRendering();
                });
        });
#ifdef LIGHTHOUSE_IMGUI
        GetRenderer().CreateView<ImGuiView>([] { ImGui::ShowDemoWindow(); });
#endif
        GetRenderer().Compile();
    }
    void OnUpdate(float) override { GetRenderer().Render(); }
};
int main() {
    Lighthouse::Config config{.name = "Lighthouse Window and Scene Example",
        .graphics = {.selection = Lighthouse::BackendSelection::FirstAvailable}};
    config.AddBackend<Lighthouse::OpenGLBackend>();
    config.AddBackend<Lighthouse::VulkanBackend>();
    return Lighthouse::Run<WindowSceneExample>(config);
}
