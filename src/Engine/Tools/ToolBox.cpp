#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Tools/ToolBox.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
namespace Lighthouse
{
RenderGraphResource ToolBox::BuildGraph(ImguiBuildContext& context,
                                        RenderGraphResource completionFlag)
{
  struct Data
  {
    RenderGraphResource completion;
  };
  // Keep the compiled tool snapshot alive until its graph is released.
  const auto tools = m_tools;
  const auto dock = ImHashStr("LighthouseToolBoxDock");
  auto completion =
      context
          .AddPass<Data>(
              "Toolbox Layout",
              [completionFlag](RenderGraph::Builder& builder, Data& data)
              { data.completion = builder.Write(completionFlag); },
              [tools, dock](const Data&, FrameGraphPassResources&,
                            RenderGraphExecutePassContext&)
              {
                if (!ImGui::DockBuilderGetNode(dock))
                {
                  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
                  ImGui::DockBuilderSetNodeSize(dock, ImVec2(700, 500));
                  for (const auto& tool : tools)
                    ImGui::DockBuilderDockWindow(
                        std::string(tool->Name()).c_str(), dock);
                  ImGui::DockBuilderFinish(dock);
                }
                ImGui::SetNextWindowSize(ImVec2(720, 540),
                                         ImGuiCond_FirstUseEver);
                const bool visible = ImGui::Begin("Toolbox");
                ImGui::DockSpace(dock, ImVec2(0, 0),
                                 visible ? ImGuiDockNodeFlags_None
                                         : ImGuiDockNodeFlags_KeepAliveOnly);
                ImGui::End();
              })
          .completion;
  ImguiBuildContext toolContext{context.graph, context.device, dock};
  for (const auto& tool : tools)
    completion = tool->BuildGraph(toolContext, completion);
  return completion;
}
} // namespace Lighthouse
#endif
