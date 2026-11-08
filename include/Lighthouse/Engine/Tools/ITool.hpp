#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/ImguiGraphObject.hpp>
#include <string_view>
namespace Lighthouse
{
class EngineRuntime;
class ITool : public ImguiGraphObject
{
public:
  virtual std::string_view Name() const noexcept = 0;
  // Custom graph tools own their window scopes and must write completionFlag.
  RenderGraphResource
  BuildGraph(ImguiBuildContext& context,
             RenderGraphResource completionFlag) override = 0;
};
} // namespace Lighthouse
#endif
