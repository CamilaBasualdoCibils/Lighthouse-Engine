#pragma once

namespace Lighthouse {
class WindowManager;

// Called with the owning ImGui context current. Native windows and graphics
// resources are owned by the backend, never by engine WindowView swapchains.
class IImGuiDevice {
public:
    virtual ~IImGuiDevice() = default;
    virtual void InitializeImGui(WindowManager& windows) = 0;
    virtual void ShutdownImGui() noexcept = 0;
    // EndImGuiFrame restores the caller's graphics context, including on failure.
    virtual void BeginImGuiFrame() = 0;
    virtual void RenderImGuiViewports() = 0;
    virtual void EndImGuiFrame() noexcept = 0;
};
} // namespace Lighthouse
