#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <functional>

struct ImGuiContext;
struct ImPlotContext;
namespace Lighthouse {
class IImGuiDevice;

// Independent desktop UI. Its platform windows never target engine windows.
class ImGuiView final : public IOutputView {
public:
    explicit ImGuiView(std::function<void()> draw);
    ~ImGuiView() override;
    void BuildGraph(ViewBuildContext&) override {}
    void Begin(Renderer* renderer) override;
    void End(Renderer* renderer) override;
private:
    std::function<void()> m_draw;
    ImGuiContext* m_context{};
    ImPlotContext* m_plotContext{};
    IImGuiDevice* m_backend{};
};
} // namespace Lighthouse
#endif
