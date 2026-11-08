#include <Lighthouse/Engine/Tools/ISimpleTool.hpp>
#ifdef LIGHTHOUSE_IMGUI
#include <imgui.h>
#include <string>
namespace Lighthouse
{
RenderGraphResource ISimpleTool::BuildGraph(ImguiBuildContext& context,
                                            RenderGraphResource completionFlag)
{
  struct Data
  {
    RenderGraphResource completion;
  };
  return context
      .AddPass<Data>(
          Name(), [completionFlag](RenderGraph::Builder& builder, Data& data)
          { data.completion = builder.Write(completionFlag); },
          [this, dockspace = context.dockspace](const Data&,
                                                FrameGraphPassResources&,
                                                RenderGraphExecutePassContext&)
          {
            if (dockspace)
              ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
            const bool open = ImGui::Begin(std::string(Name()).c_str());
            try
            {
              if (open)
                Draw();
            }
            catch (...)
            {
              ImGui::End();
              throw;
            }
            ImGui::End();
          })
      .completion;
}
} // namespace Lighthouse
#endif
