#pragma once

#include <Lighthouse/Engine/Core/Config.hpp>
#include <Lighthouse/Engine/Tools/ToolBox.hpp>
#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

namespace Lighthouse {

class EngineRuntime {
public:
    EngineRuntime() {}
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
#ifdef LIGHTHOUSE_IMGUI
    ToolBox& GetToolBox() noexcept { return m_toolBox; }
#endif
    InputSystem& GetInputSystem() noexcept { return m_input; }
    const InputSystem& GetInputSystem() const noexcept { return m_input; }
    WindowManager& GetWindowManager() noexcept { return m_windows; }
    RenderGraphComposer& GetRenderer() noexcept { return *m_renderer; }

private:
#ifdef LIGHTHOUSE_IMGUI
    ToolBox m_toolBox{*this};
#endif
    WindowManager m_windows;
    InputSystem m_input;
    std::unique_ptr<RenderGraphComposer> m_renderer;
    BackendSelectionResult m_backend;
};

} // namespace Lighthouse
