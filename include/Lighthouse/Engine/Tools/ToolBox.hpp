#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Tools/ITool.hpp>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
namespace Lighthouse
{
class EngineRuntime;
class ToolBox : public ImguiGraphObject
{
public:
  explicit ToolBox(EngineRuntime& runtime) : m_runtime(runtime) {}
  // Registration changes take effect after recompiling the render graph.
  template <class T, class... Args> T& AddTool(Args&&... args)
  {
    static_assert(std::is_base_of_v<ITool, T>);
    auto tool = std::make_shared<T>(m_runtime, std::forward<Args>(args)...);
    if (tool->Name().empty())
      throw std::invalid_argument("Tool name must not be empty");
    for (const auto& existing : m_tools)
      if (existing->Name() == tool->Name())
        throw std::invalid_argument("Tool name is already registered");
    m_tools.push_back(tool);
    return *tool;
  }
  void Clear() noexcept
  {
    m_tools.clear();
  }
  RenderGraphResource BuildGraph(ImguiBuildContext& context,
                                 RenderGraphResource completionFlag) override;

private:
  EngineRuntime& m_runtime;
  std::vector<std::shared_ptr<ITool>> m_tools;
};
} // namespace Lighthouse
#endif
