#include <Lighthouse/Engine/Rendering/ImGuiView.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/IImGuiDevice.hpp>
#include <Lighthouse/Engine/Rendering/ImguiSimpleObject.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <imgui.h>
#include <implot.h>
#include <stdexcept>

namespace Lighthouse
{
namespace
{
struct CurrentContexts
{
  ImGuiContext* imgui = ImGui::GetCurrentContext();
  ImPlotContext* implot = ImPlot::GetCurrentContext();
  ~CurrentContexts()
  {
    ImGui::SetCurrentContext(imgui);
    ImPlot::SetCurrentContext(implot);
  }
};
} // namespace
namespace
{
struct ImguiCompletion
{
  struct Desc
  {
  };
  void create(const Desc&, void*) {}
  void destroy(const Desc&, void*) {}
  void preRead(const Desc&, std::uint32_t, void*) {}
  void preWrite(const Desc&, std::uint32_t, void*) {}
};
class CallbackObject final : public ImguiSimpleObject
{
public:
  explicit CallbackObject(std::function<void()> draw) : m_draw(std::move(draw))
  {
  }
  void Draw() override
  {
    m_draw();
  }

private:
  std::function<void()> m_draw;
};
} // namespace
ImGuiView::ImGuiView(std::function<void()> draw)
{
  if (!draw)
    throw std::invalid_argument("ImGuiView requires a UI callback");
  AddObject(std::make_shared<CallbackObject>(std::move(draw)));
}
void ImGuiView::AddObject(std::shared_ptr<ImguiGraphObject> object)
{
  if (!object)
    throw std::invalid_argument("ImGui graph object must not be null");
  m_objects.push_back(std::move(object));
}
void ImGuiView::AddObject(ImguiGraphObject& object)
{
  AddObject(
      std::shared_ptr<ImguiGraphObject>(&object, [](ImguiGraphObject*) {}));
}
void ImGuiView::PostBuildGraph(ViewBuildContext& context)
{
  const auto inspectionScope = context.graph.HideFromInspector();
  struct Data
  {
    RenderGraphResource completion;
  };
  auto completion = context.graph.Import<ImguiCompletion>("Completion", {}, {});
  ImguiBuildContext imgui{context.graph, context.device};
  completion = imgui
                   .AddPass<Data>(
                       "ImGui NewFrame",
                       [completion](RenderGraph::Builder& builder, Data& data)
                       { data.completion = builder.Write(completion); },
                       [this](const Data&, FrameGraphPassResources&,
                              RenderGraphExecutePassContext&) { NewFrame(); })
                   .completion;
  for (const auto& object : m_objects)
    completion = object->BuildGraph(imgui, completion);
  imgui.AddPass(
      "ImGui Render",
      [completion](RenderGraph::Builder& builder, FrameGraph::NoData&)
      {
        builder.Read(completion);
        builder.SetSideEffect();
      },
      [this](const FrameGraph::NoData&, FrameGraphPassResources&,
             RenderGraphExecutePassContext&) { RenderFrame(); });
}
ImGuiView::~ImGuiView()
{
  AbortFrame();
  if (!m_context)
    return;
  CurrentContexts restore;
  // Do not restore a context we are about to destroy.
  if (restore.imgui == m_context)
    restore.imgui = nullptr;
  if (restore.implot == m_plotContext)
    restore.implot = nullptr;
  ImGui::SetCurrentContext(m_context);
  ImPlot::SetCurrentContext(m_plotContext);
  m_backend->ShutdownImGui();
  ImPlot::DestroyContext(m_plotContext);
  ImGui::DestroyContext(m_context);
}
void ImGuiView::Begin(RenderGraphComposer* renderer)
{
  if (m_context)
    return;
  auto* backend = dynamic_cast<IImGuiDevice*>(renderer->GetRenderDevice());
  if (!backend)
    throw std::runtime_error(
        "ImGuiView is unsupported by the selected graphics backend");
  auto* windows = renderer->GetWindowManager();
  if (!windows)
    throw std::logic_error("ImGuiView requires a WindowManager");
  CurrentContexts restore;
  IMGUI_CHECKVERSION();
  auto* context = ImGui::CreateContext();
  ImGui::SetCurrentContext(context);
  auto* plot = ImPlot::CreateContext();
  auto& io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
  io.ConfigViewportsNoAutoMerge = true;
  io.ConfigViewportsNoDecoration = true;
  try
  {
    backend->InitializeImGui(*windows);
  }
  catch (...)
  {
    ImPlot::DestroyContext(plot);
    ImGui::DestroyContext(context);
    throw;
  }
  m_context = context;
  m_plotContext = plot;
  m_backend = backend;
}
void ImGuiView::NewFrame()
{
  if (!m_context)
    throw std::logic_error(
        "ImGuiView must be initialized before graph execution");
  if (m_frameActive)
    throw std::logic_error("ImGui frame already active");
  m_previousContext = ImGui::GetCurrentContext();
  m_previousPlotContext = ImPlot::GetCurrentContext();
  ImGui::SetCurrentContext(m_context);
  ImPlot::SetCurrentContext(m_plotContext);
  try
  {
    m_backend->BeginImGuiFrame();
    m_frameActive = true;
    ImGui::NewFrame();
    m_frameStarted = true;
  }
  catch (...)
  {
    if (m_frameActive)
      AbortFrame();
    else
    {
      ImGui::SetCurrentContext(m_previousContext);
      ImPlot::SetCurrentContext(m_previousPlotContext);
    }
    throw;
  }
}
void ImGuiView::RestoreFrame() noexcept
{
  m_backend->EndImGuiFrame();
  m_frameActive = false;
  m_frameStarted = false;
  ImGui::SetCurrentContext(m_previousContext);
  ImPlot::SetCurrentContext(m_previousPlotContext);
}
void ImGuiView::RenderFrame()
{
  if (!m_frameActive)
    throw std::logic_error("ImGui frame has not started");
  try
  {
    ImGui::EndFrame();
    ImGui::Render();
    m_backend->RenderImGuiViewports();
  }
  catch (...)
  {
    AbortFrame();
    throw;
  }
  RestoreFrame();
}
void ImGuiView::AbortFrame() noexcept
{
  if (!m_frameActive)
    return;
  ImGui::SetCurrentContext(m_context);
  ImPlot::SetCurrentContext(m_plotContext);
  if (m_frameStarted)
  {
    try
    {
      // Complete platform-frame bookkeeping even after an object fails.
      ImGui::Render();
      m_backend->RenderImGuiViewports();
    }
    catch (...)
    {
    }
  }
  RestoreFrame();
}
void ImGuiView::End(RenderGraphComposer*)
{
  // Successful frames finish in the graph. This is exception cleanup only.
  AbortFrame();
}
} // namespace Lighthouse
#endif
