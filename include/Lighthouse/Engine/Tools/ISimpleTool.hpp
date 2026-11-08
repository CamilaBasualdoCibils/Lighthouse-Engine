#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Tools/ITool.hpp>
namespace Lighthouse
{
class ISimpleTool : public ITool
{
public:
  RenderGraphResource BuildGraph(ImguiBuildContext& context,
                                 RenderGraphResource completionFlag) final;
  // Draw contents only; the default graph pass owns the ImGui window.
  virtual void Draw() = 0;
};
} // namespace Lighthouse
#endif
