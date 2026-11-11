#pragma once

namespace Lighthouse {
class WindowManager;
class WindowView;

// Called with the owning ImGui context current. A host is borrowed from the
// engine; without one the backend creates a private hidden main viewport.
class IImGuiDevice {
public:
    virtual ~IImGuiDevice() = default;
    virtual void InitializeImGui(WindowManager& windows, WindowView* host = nullptr) = 0;
    // Called after graph submission, before presentation. Preserve scene color.
    virtual void RenderImGuiHost(WindowView& host) = 0;
    virtual void ShutdownImGui() noexcept = 0;
    // EndImGuiFrame restores the caller's graphics context, including on failure.
    virtual void BeginImGuiFrame() = 0;
    virtual void RenderImGuiViewports() = 0;
    virtual void EndImGuiFrame() noexcept = 0;
};
} // namespace Lighthouse
