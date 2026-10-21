#pragma once

#include <utility>
#include <variant>

#include <Lighthouse/Engine/Core/Handle.hpp>

namespace Lighthouse {

struct RenderOutput {
    std::variant<std::monostate, WindowHandle, OffscreenTargetHandle> target;
    static RenderOutput ForWindow(WindowHandle handle) { return {{handle}}; }
    static RenderOutput Offscreen(OffscreenTargetHandle handle) { return {{handle}}; }
    bool IsSet() const noexcept { return target.index() != 0; }
};

struct PresentationSettings {
    bool vsync{true};
};

struct RenderSessionDesc {
    RenderViewHandle view;
    RenderOutput output;
    bool enabled{true};
};

class RenderSession {
public:
    explicit RenderSession(RenderSessionDesc desc = {}) : m_desc(std::move(desc)) {}
    void SetView(RenderViewHandle view) noexcept { m_desc.view = view; }
    void SetOutput(RenderOutput output) { m_desc.output = std::move(output); }
    void SetEnabled(bool enabled) noexcept { m_desc.enabled = enabled; }
    RenderViewHandle GetView() const noexcept { return m_desc.view; }
    const RenderOutput& GetOutput() const noexcept { return m_desc.output; }
    bool IsEnabled() const noexcept { return m_desc.enabled; }
    PresentationSettings& Presentation() noexcept { return m_presentation; }
    const PresentationSettings& Presentation() const noexcept { return m_presentation; }

private:
    RenderSessionDesc m_desc;
    PresentationSettings m_presentation;
};

} // namespace Lighthouse
