#include <Lighthouse/Engine/Input/InputAction.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Lighthouse {
void InputAction::Bind(std::shared_ptr<IInputDevice> device, InputControl control, glm::vec2 direction) {
    if (!device) throw std::invalid_argument("Input binding requires an opened device");
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y))
        throw std::invalid_argument("Input binding direction must be finite");
    if (control.kind == InputControl::Kind::CursorDelta && m_type != InputActionType::Delta2D)
        throw std::invalid_argument("Mouse deltas require a Delta2D action");
    m_bindings.push_back({std::move(device), control, direction});
}
void InputAction::BindComposite2D(std::shared_ptr<IInputDevice> device, Key left,
                                 Key right, Key down, Key up) {
    if (m_type != InputActionType::Axis2D) throw std::invalid_argument("Composite requires Axis2D");
    Bind(device, InputControl::Button(left), {-1, 0});
    Bind(device, InputControl::Button(right), {1, 0});
    Bind(device, InputControl::Button(down), {0, -1});
    Bind(device, InputControl::Button(up), {0, 1});
}
void InputAction::Evaluate(const States& states, bool enabled, std::vector<std::function<void()>>& dispatch) {
    glm::vec2 value{};
    for (auto& binding : m_bindings) {
        const auto found = states.find(binding.device.get());
        if (!enabled || found == states.end() || !found->second.connected) {
            binding.tracking = false;
            continue;
        }
        const auto& state = found->second;
        const auto index = binding.control.index;
        float sample = 0;
        if (binding.control.kind == InputControl::Kind::Button) {
            if (index < state.buttons.size()) sample = state.buttons[index] ? 1.f : 0.f;
        } else if (binding.control.kind == InputControl::Kind::CursorDelta) {
            const double position = index == 0 ? state.cursorX : state.cursorY;
            if (!std::isfinite(position)) { binding.tracking = false; continue; }
            if (binding.tracking) sample = static_cast<float>(position - binding.previous);
            binding.previous = position;
            binding.tracking = true;
            if (!std::isfinite(sample)) sample = 0;
        } else if (index < state.axes.size() && std::isfinite(state.axes[index])) sample = state.axes[index];
        if (m_type == InputActionType::Button) value.x = (value.x != 0 || std::abs(sample) > 0.5f) ? 1.f : 0.f;
        else value += binding.direction * sample;
    }
    if (m_type == InputActionType::Axis1D) value = {std::clamp(value.x, -1.f, 1.f), 0};
    if (m_type == InputActionType::Axis2D) {
        const auto length = std::hypot(value.x, value.y);
        if (length > 1) value /= length;
    }
    if (value == m_value) return;
    const bool wasPressed = m_value.x != 0;
    m_value = value;
    if (m_type == InputActionType::Button) {
        const auto& callbacks = wasPressed ? m_released : m_pressed;
        dispatch.insert(dispatch.end(), callbacks.begin(), callbacks.end());
    }
    for (const auto& callback : m_changed) dispatch.push_back([callback, value] { callback(value); });
}
std::shared_ptr<InputAction> InputActionSet::AddAction(std::string name, InputActionType type) {
    for (const auto& action : m_actions) if (action->Name() == name)
        throw std::invalid_argument("Duplicate input action name");
    auto action = std::make_shared<InputAction>(std::move(name), type);
    m_actions.push_back(action);
    return action;
}
} // namespace Lighthouse
