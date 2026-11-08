#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <Lighthouse/Engine/Tools/InputSystemTool.hpp>
#include <boost/describe/enum_to_string.hpp>
#include <imgui.h>
#include <unordered_set>
namespace Lighthouse
{
namespace
{
const char* ControlName(const Lighthouse::InputAction::BindingInfo& binding)
{
  using namespace Lighthouse;
  const auto control = binding.control;
  if (control.kind == InputControl::Kind::CursorDelta)
    return boost::describe::enum_to_string(
        static_cast<MouseAxis>(control.index), "Unknown");
  if (control.kind == InputControl::Kind::Axis)
    return boost::describe::enum_to_string(
        static_cast<GamepadAxis>(control.index), "Unmapped axis");
  switch (binding.device->Info().type)
  {
  case InputDeviceType::Keyboard:
    return boost::describe::enum_to_string(static_cast<Key>(control.index),
                                           "Unmapped key");
  case InputDeviceType::Mouse:
    return boost::describe::enum_to_string(
        static_cast<MouseButton>(control.index), "Unmapped button");
  default:
    return boost::describe::enum_to_string(
        static_cast<GamepadButton>(control.index), "Unmapped button");
  }
}
} // namespace
InputSystemTool::InputSystemTool(EngineRuntime& runtime)
    : m_input(runtime.GetInputSystem())
{
}
void InputSystemTool::Draw()
{
  ImGui::TextUnformatted("Backends and available devices");
  for (auto* backend : m_input.GetBackends())
  {
    ImGui::PushID(backend);
    if (ImGui::TreeNode("Backend", "%.*s", int(backend->Name().size()),
                        backend->Name().data()))
    {
      try
      {
        for (auto type : {Lighthouse::InputDeviceType::Keyboard,
                          Lighthouse::InputDeviceType::Mouse,
                          Lighthouse::InputDeviceType::Controller})
          for (const auto& device : backend->Enumerate(type))
            ImGui::BulletText(
                "%s [%llu:%llu]", device.name.c_str(),
                static_cast<unsigned long long>(device.id.backend),
                static_cast<unsigned long long>(device.id.device));
      }
      catch (const std::exception& error)
      {
        ImGui::TextWrapped("%s", error.what());
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  ImGui::SeparatorText("Input sets");
  bool active = false;
  for (const auto& set : m_input.GetActionSets())
  {
    active = true;
    ImGui::PushID(set.get());
    if (ImGui::TreeNodeEx(set->Name().c_str(), ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::TextUnformatted(set->IsEnabled() ? "Enabled" : "Disabled");
      for (const auto& action : set->GetActions())
      {
        ImGui::PushID(action.get());
        if (ImGui::TreeNode(action->Name().c_str()))
        {
          const auto value = action->GetAxis2D();
          ImGui::Text("Value: %.3f, %.3f", value.x, value.y);
          std::unordered_set<const Lighthouse::IInputDevice*> shown;
          auto bindings = action->GetBindings();
          for (const auto& binding : bindings)
          {
            if (!shown.insert(binding.device.get()).second)
              continue;
            ImGui::PushID(binding.device.get());
            if (ImGui::TreeNode(binding.device->Info().name.c_str()))
            {
              for (const auto& control : bindings)
                if (control.device == binding.device)
                  ImGui::BulletText("%s [%zu] -> (%.1f, %.1f)",
                                    ControlName(control), control.control.index,
                                    control.direction.x, control.direction.y);
              ImGui::TreePop();
            }
            ImGui::PopID();
          }
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (!active)
    ImGui::TextUnformatted("No active input sets.");
}
} // namespace Lighthouse
#endif
