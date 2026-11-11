#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImguiGraphObject.hpp>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>
#include <functional>
#include <memory>
#include <vector>

struct ImGuiContext;
struct ImPlotContext;
namespace Lighthouse
{
class IImGuiDevice;
struct ImGuiViewDesc
{
  bool showImguiDemo{false},showImplotDemo{false};
};
// Desktop UI, optionally hosted by the renderer's ImGuiHost WindowView.
class ImGuiView final : public IOutputView
{
public:
  ImGuiView(const ImGuiViewDesc& desc = {});
  explicit ImGuiView(std::function<void()> draw);
  // Recompile the renderer after changing graph objects.
  void AddObject(std::shared_ptr<ImguiGraphObject> object);
  // Borrowed objects must outlive the view and any compiled graph using it.
  void AddObject(ImguiGraphObject& object);
  ~ImGuiView() override;
  // Renderer configures this at compilation, before graph execution.
  void SetHost(WindowView* host);
  void BuildGraph(ViewBuildContext&) override {}
  void PostBuildGraph(ViewBuildContext& context) override;
  void Begin(RenderGraphComposer* renderer) override;
  void End(RenderGraphComposer* renderer) override;

private:
  void Shutdown();
  void NewFrame();
  void RenderFrame();
  void RestoreFrame() noexcept;
  void AbortFrame() noexcept;
  std::vector<std::shared_ptr<ImguiGraphObject>> m_objects;
  ImGuiContext* m_previousContext{};
  ImPlotContext* m_previousPlotContext{};
  bool m_frameActive{};
  bool m_frameStarted{};
  ImGuiContext* m_context{};
  ImPlotContext* m_plotContext{};
  IImGuiDevice* m_backend{};
  WindowView* m_host{};
  const ImGuiViewDesc m_desc{};
};
} // namespace Lighthouse
#endif
