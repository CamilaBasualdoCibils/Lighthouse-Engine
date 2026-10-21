#include <Lighthouse/Engine/Engine.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>

#include <memory>

class WindowSceneExample : public Lighthouse::Application {
public:
    void OnStart() override {
        auto& scene = CreateScene("Main");
        auto camera = scene.CreateEntity("Camera");
        camera.Add<Lighthouse::Transform>();
        camera.Add<Lighthouse::Camera>(Lighthouse::Camera{70.0f});
        scene.SetActiveCamera(camera);

        const auto handle = GetWindowManager().CreateWindow({
            .title = "Lighthouse Window and Scene",
            .width = 960,
            .height = 540,
            .eglSurface = true});
        auto* window = GetWindowManager().GetWindow(handle);
        window->Show();

        if (auto* backend = GetGraphicsBackend()) {
            m_device = backend->CreateDevice(Lighthouse::DeviceConfig{});
            m_swapchain = m_device->CreateSwapchain(*window, Lighthouse::SwapchainConfig{});
            m_window = window;
        }
    }

    void OnUpdate(float) override {
        if (!m_device || !m_window) return;

        auto commands = m_device->CreateCommandList(Lighthouse::QueueType::Graphics);
        Lighthouse::RenderPassDesc pass;
        pass.clearColor = true;
        pass.clearColorValue = {0.08f, 0.10f, 0.14f};
        pass.width = m_window->FramebufferWidth();
        pass.height = m_window->FramebufferHeight();
        commands->BeginRendering(pass);
        commands->EndRendering();
        m_device->Submit(m_swapchain, *commands);
    }

private:
    std::unique_ptr<Lighthouse::IRenderDevice> m_device;
    Lighthouse::SwapchainHandle m_swapchain;
    Lighthouse::Window* m_window{};
};

int main() {
    Lighthouse::Config config{.name = "Lighthouse Window and Scene Example",
        .graphics = {.selection = Lighthouse::BackendSelection::FirstAvailable}};
    config.AddBackend<Lighthouse::OpenGLBackend>();
    return Lighthouse::Run<WindowSceneExample>(config);
}
