#pragma once

#include <Lighthouse/Engine/Core/Config.hpp>
#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

namespace Lighthouse {

class EngineRuntime {
public:
    EngineRuntime() : m_renderer(&m_windows) {}
    ~EngineRuntime();
    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    bool Initialize(const Config& config);
    bool Initialize(const Config& config, const BackendRegistry& backends);
    void Shutdown() noexcept;
    bool HasGraphicsBackend() const noexcept { return static_cast<bool>(m_backend.backend); }
    IGraphicsBackend* GetGraphicsBackend() noexcept { return m_backend.backend.get(); }
    const IGraphicsBackend* GetGraphicsBackend() const noexcept { return m_backend.backend.get(); }
    const std::vector<BackendAttempt>& BackendDiagnostics() const noexcept { return m_backend.attempts; }
    WindowManager& GetWindowManager() noexcept { return m_windows; }
    Renderer& GetRenderer() noexcept { return m_renderer; }

private:
    WindowManager m_windows;
    Renderer m_renderer;
    BackendSelectionResult m_backend;
};

} // namespace Lighthouse
