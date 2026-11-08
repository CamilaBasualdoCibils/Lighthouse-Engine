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

// Independent desktop UI. Its platform windows never target engine windows.
class ImGuiView final : public IOutputView
{
public:
  ImGuiView() = default;
  explicit ImGuiView(std::function<void()> draw);
  // Recompile the renderer after changing graph objects.
  void AddObject(std::shared_ptr<ImguiGraphObject> object);
  // Borrowed objects must outlive the view and any compiled graph using it.
  void AddObject(ImguiGraphObject& object);
  ~ImGuiView() override;
  void BuildGraph(ViewBuildContext&) override {}
  void PostBuildGraph(ViewBuildContext& context) override;
  void Begin(Renderer* renderer) override;
  void End(Renderer* renderer) override;

private:
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
};
} // namespace Lighthouse
#endif
