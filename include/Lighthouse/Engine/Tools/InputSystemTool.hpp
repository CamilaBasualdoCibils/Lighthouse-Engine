#pragma once
#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Tools/ISimpleTool.hpp>
namespace Lighthouse
{
class InputSystem;
class InputSystemTool final : public ISimpleTool
{
public:
  explicit InputSystemTool(EngineRuntime& runtime);
  std::string_view Name() const noexcept override
  {
    return "Input";
  }
  void Draw() override;

private:
  InputSystem& m_input;
};
} // namespace Lighthouse
#endif
