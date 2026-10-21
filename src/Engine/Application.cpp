#include <Lighthouse/Engine/Core/Application.hpp>

#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>

namespace Lighthouse {

struct Application::Storage {
    EngineRuntime* runtime{};
    const Config* config{};
    std::vector<std::unique_ptr<Scene>> scenes;
};

Application::Application() : m_storage(std::make_shared<Storage>()) {}
Application::~Application() = default;

Scene& Application::CreateScene(std::string name) {
    m_storage->scenes.push_back(std::make_unique<Scene>(std::move(name)));
    return *m_storage->scenes.back();
}

void Application::AttachRuntime(EngineRuntime& runtime, const Config& config) {
    m_storage->runtime = &runtime;
    m_storage->config = &config;
}

EngineRuntime& Application::GetRuntime() {
    if (!m_storage->runtime) throw std::logic_error("Application runtime is only available during Run<T>");
    return *m_storage->runtime;
}

WindowManager& Application::GetWindowManager() { return GetRuntime().GetWindowManager(); }
Renderer& Application::GetRenderer() { return GetRuntime().GetRenderer(); }
IGraphicsBackend* Application::GetGraphicsBackend() { return GetRuntime().GetGraphicsBackend(); }

const Config& Application::GetConfig() const {
    if (!m_storage->config) throw std::logic_error("Application config is only available during Run<T>");
    return *m_storage->config;
}

bool Application::ShouldClose() const {
    if (!m_storage->runtime) return true;
    const auto& windows = m_storage->runtime->GetWindowManager();
    if (windows.ShouldClose()) return true;
    return false;
}

} // namespace Lighthouse
