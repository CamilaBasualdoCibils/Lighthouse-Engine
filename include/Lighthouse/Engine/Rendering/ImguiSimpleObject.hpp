#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImguiGraphObject.hpp>
namespace Lighthouse
{
class ImguiSimpleObject : public ImguiGraphObject
{
public:
  RenderGraphResource BuildGraph(ImguiBuildContext& context,
                                 RenderGraphResource completionFlag) final;
  virtual void Draw() = 0;
};
} // namespace Lighthouse
#endif
