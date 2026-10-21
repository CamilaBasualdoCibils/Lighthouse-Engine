#include <Lighthouse/Engine/Engine.hpp>

#include <memory>

class WindowSceneExample {
public:
    int Run() {
        Engine::Config config{
            .name = "Lighthouse Window and Scene Example",
            .graphics = {
                .backends = {
                    Engine::GraphicsBackend::Vulkan,
                    Engine::GraphicsBackend::D3D12,
                    Engine::GraphicsBackend::OpenGL
                },
                .selection = Engine::BackendSelection::FirstAvailable
            }
        };

        // Register graphics backend implementations here as they are added.
        if (!m_runtime.Initialize(config, m_backends)) return 1;

        m_window = m_runtime.GetWindowManager().CreateWindow({
            .title = "Lighthouse Example",
            .width = 1280,
            .height = 720
        });

        m_scene = std::make_unique<Engine::Scene>("Main");
        auto camera = m_scene->CreateEntity("Camera");
        camera.Add<Engine::Transform>();
        camera.Add<Engine::Camera>(Engine::Camera{70.0f});
        m_scene->SetActiveCamera(camera);

        auto& windows = m_runtime.GetWindowManager();
        while (const auto* window = windows.GetWindow(m_window)) {
            if (window->ShouldClose()) break;
            windows.PollEvents();
        }

        m_runtime.Shutdown();
        return 0;
    }

private:
    Engine::BackendRegistry m_backends;
    Engine::EngineRuntime m_runtime;
    Engine::WindowHandle m_window;
    std::unique_ptr<Engine::Scene> m_scene;
};

int main() {
    WindowSceneExample application;
    return application.Run();
}
