#include <Lighthouse/Engine/Rendering/ImguiSimpleObject.hpp>
#ifdef LIGHTHOUSE_IMGUI
namespace Lighthouse
{
RenderGraphResource
ImguiSimpleObject::BuildGraph(ImguiBuildContext& context,
                              RenderGraphResource completionFlag)
{
  struct Data
  {
    RenderGraphResource completion;
  };
  return context
      .AddPass<Data>(
          "ImGui Object",
          [completionFlag](RenderGraph::Builder& builder, Data& data)
          { data.completion = builder.Write(completionFlag); },
          [this](const Data&, FrameGraphPassResources&,
                 RenderGraphExecutePassContext&) { Draw(); })
      .completion;
}
} // namespace Lighthouse
#endif
