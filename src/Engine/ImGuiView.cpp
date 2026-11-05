#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <imgui.h>
#include <implot.h>
#include <stdexcept>

namespace Lighthouse {
namespace {
struct CurrentContexts {
    ImGuiContext* imgui = ImGui::GetCurrentContext();
    ImPlotContext* implot = ImPlot::GetCurrentContext();
    ~CurrentContexts() {
        ImGui::SetCurrentContext(imgui);
        ImPlot::SetCurrentContext(implot);
    }
};
}
ImGuiView::ImGuiView(std::function<void()> draw) : m_draw(std::move(draw)) {
    if (!m_draw) throw std::invalid_argument("ImGuiView requires a UI callback");
}
ImGuiView::~ImGuiView() {
    if (!m_context) return;
    CurrentContexts restore;
    // Do not restore a context we are about to destroy.
    if (restore.imgui == m_context) restore.imgui = nullptr;
    if (restore.implot == m_plotContext) restore.implot = nullptr;
    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plotContext);
    m_backend->ShutdownImGui();
    ImPlot::DestroyContext(m_plotContext);
    ImGui::DestroyContext(m_context);
}
void ImGuiView::Begin(Renderer* renderer) {
    if (m_context) return;
    auto* backend = dynamic_cast<IImGuiDevice*>(renderer->GetRenderDevice());
    if (!backend) throw std::runtime_error("ImGuiView is unsupported by the selected graphics backend");
    auto* windows = renderer->GetWindowManager();
    if (!windows) throw std::logic_error("ImGuiView requires a WindowManager");
    CurrentContexts restore;
    IMGUI_CHECKVERSION();
    auto* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    auto* plot = ImPlot::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;
    io.ConfigViewportsNoDecoration = true;
    try {
        backend->InitializeImGui(*windows);
    } catch (...) {
        ImPlot::DestroyContext(plot);
        ImGui::DestroyContext(context);
        throw;
    }
    m_context = context;
    m_plotContext = plot;
    m_backend = backend;
}
void ImGuiView::End(Renderer*) {
    CurrentContexts restore;
    ImGui::SetCurrentContext(m_context);
    ImPlot::SetCurrentContext(m_plotContext);
    m_backend->BeginImGuiFrame();
    try {
        ImGui::NewFrame();
        m_draw();
        ImGui::Render();
        m_backend->RenderImGuiViewports();
    } catch (...) {
        // Complete ImGui's platform-frame bookkeeping so the next frame can
        // recover after a UI callback throws. Preserve the original exception.
        try {
            ImGui::Render();
            m_backend->RenderImGuiViewports();
        } catch (...) {}
        m_backend->EndImGuiFrame();
        throw;
    }
    m_backend->EndImGuiFrame();
}
} // namespace Lighthouse
#endif
