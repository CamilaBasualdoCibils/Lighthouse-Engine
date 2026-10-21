#pragma once

#ifdef LIGHTHOUSE_IMGUI

#include <memory>
#include <functional>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>

struct ImGuiContext;
struct ImPlotContext;

namespace Lighthouse {

class Window;
class IImGuiDevice;

// The application owns the session through its last frame and destroys it before the window/device.
class ImGuiSession final {
public:
    explicit ImGuiSession(Window& window) : m_window(window) {}
    ~ImGuiSession();
    ImGuiSession(const ImGuiSession&) = delete;
    ImGuiSession& operator=(const ImGuiSession&) = delete;

    void Begin(IRenderDevice& device, SwapchainHandle swapchain);
    void End(IRenderDevice& device, SwapchainHandle swapchain, ImageViewHandle image);

private:
    Window& m_window;
    IImGuiDevice* m_backend{};
    ImGuiContext* m_context{};
    ImPlotContext* m_plotContext{};
    bool m_platformInitialized{};
    bool m_rendererInitialized{};
};

class ImGuiTechnique final : public IRenderTechnique {
public:
    struct PassData { GraphImageHandle image{InvalidGraphImage}; };
    template <typename Source>
    ImGuiTechnique(std::shared_ptr<ImGuiSession> session, TechniqueRef<Source> source,
                   std::function<void()> draw)
        : m_session(std::move(session)),
          m_sourceImage([source](TechniqueBuildContext& ctx) {
              return ctx.GetTechnique(source).GetPassData().image;
          }),
          m_draw(std::move(draw)) {}
    void BuildGraph(TechniqueBuildContext& ctx) override;
    const PassData& GetPassData() const noexcept { return m_passData; }
private:
    std::weak_ptr<ImGuiSession> m_session;
    std::function<GraphImageHandle(TechniqueBuildContext&)> m_sourceImage;
    std::function<void()> m_draw;
    PassData m_passData;
};

} // namespace Lighthouse
#endif
