#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>
namespace Lighthouse
{
struct ImguiBuildContext
{
  RenderGraph& graph;
  IRenderDevice& device;
  // Optional default docking destination supplied by a containing graph object.
  std::uint32_t dockspace{};

  // RenderGraph owns command-list recording around the execute callback.
  template <class Data = FrameGraph::NoData, class Setup, class Execute>
  const Data& AddPass(std::string_view name, Setup&& setup, Execute&& execute)
  {
    return graph.AddPass<Data>(name, std::forward<Setup>(setup),
                               std::forward<Execute>(execute));
  }
};
class ImguiGraphObject
{
public:
  virtual ~ImguiGraphObject() = default;
  // Schedule UI operations, write completionFlag, and return its new version.
  // Execution occurs between the view's NewFrame and Render graph passes.
  virtual RenderGraphResource
  BuildGraph(ImguiBuildContext& context,
             RenderGraphResource completionFlag) = 0;
};
} // namespace Lighthouse
#endif
