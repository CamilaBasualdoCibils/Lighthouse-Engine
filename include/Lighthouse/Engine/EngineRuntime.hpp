#pragma once

#include <Lighthouse/Engine/Config.hpp>
#include <Lighthouse/Engine/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Renderer.hpp>
#include <Lighthouse/Engine/WindowManager.hpp>

namespace Engine {

class EngineRuntime {
public:
    EngineRuntime() : m_renderer(&m_windows) {}
    ~EngineRuntime();
    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    bool Initialize(const Config& config, const BackendRegistry& backends);
    void Shutdown() noexcept;
    bool HasGraphicsBackend() const noexcept { return static_cast<bool>(m_backend.backend); }
    const std::vector<BackendAttempt>& BackendDiagnostics() const noexcept { return m_backend.attempts; }
    WindowManager& GetWindowManager() noexcept { return m_windows; }
    Renderer& GetRenderer() noexcept { return m_renderer; }

private:
    WindowManager m_windows;
    Renderer m_renderer;
    BackendSelectionResult m_backend;
};

} // namespace Engine
