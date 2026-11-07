#pragma once
#include <Lighthouse/Engine/Input/InputBackend.hpp>
#include <Lighthouse/Engine/Input/InputTypes.hpp>
#include <functional>
#include <glm/vec2.hpp>
#include <unordered_map>

namespace Lighthouse {
enum class InputActionType { Button, Axis1D, Axis2D, Delta2D };
struct InputControl {
    enum class Kind { Button, Axis, CursorDelta };
    Kind kind;
    std::size_t index;
    static InputControl Button(Key key) { return {Kind::Button, static_cast<std::size_t>(key)}; }
    static InputControl Button(MouseButton button) { return {Kind::Button, static_cast<std::size_t>(button)}; }
    static InputControl Button(GamepadButton button) { return {Kind::Button, static_cast<std::size_t>(button)}; }
    static InputControl Axis(GamepadAxis axis) { return {Kind::Axis, static_cast<std::size_t>(axis)}; }
    static InputControl Delta(MouseAxis axis) { return {Kind::CursorDelta, static_cast<std::size_t>(axis)}; }
    // Explicit escape hatches for backend-specific, unmapped devices.
    static InputControl RawButton(std::size_t index) { return {Kind::Button, index}; }
    static InputControl RawAxis(std::size_t index) { return {Kind::Axis, index}; }
};
class InputSystem;
class InputAction {
public:
    InputAction(std::string name, InputActionType type) : m_name(std::move(name)), m_type(type) {}
    const std::string& Name() const noexcept { return m_name; }
    // Axis2D bindings contribute along direction; Axis1D uses direction.x.
    void Bind(std::shared_ptr<IInputDevice> device, InputControl control, glm::vec2 direction = {1, 0});
    void BindComposite2D(std::shared_ptr<IInputDevice> device, Key left,
                         Key right, Key down, Key up);
    bool IsPressed() const noexcept { return m_value.x != 0; }
    float GetAxis1D() const noexcept { return m_value.x; }
    glm::vec2 GetAxis2D() const noexcept { return m_value; }
    void OnPressed(std::function<void()> callback) { m_pressed.push_back(std::move(callback)); }
    void OnReleased(std::function<void()> callback) { m_released.push_back(std::move(callback)); }
    void OnChanged(std::function<void(glm::vec2)> callback) { m_changed.push_back(std::move(callback)); }
private:
    friend class InputSystem;
    struct Binding { std::shared_ptr<IInputDevice> device; InputControl control; glm::vec2 direction; double previous{}; bool tracking{}; };
    using States = std::unordered_map<const IInputDevice*, InputDeviceState>;
    void Evaluate(const States& states, bool enabled, std::vector<std::function<void()>>& dispatch);
    std::string m_name;
    InputActionType m_type;
    glm::vec2 m_value{};
    std::vector<Binding> m_bindings;
    std::vector<std::function<void()>> m_pressed, m_released;
    std::vector<std::function<void(glm::vec2)>> m_changed;
};
class InputActionSet {
public:
    explicit InputActionSet(std::string name) : m_name(std::move(name)) {}
    const std::string& Name() const noexcept { return m_name; }
    std::shared_ptr<InputAction> AddAction(std::string name, InputActionType type);
    void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
    bool IsEnabled() const noexcept { return m_enabled; }
private:
    friend class InputSystem;
    std::string m_name;
    bool m_enabled{};
    std::vector<std::shared_ptr<InputAction>> m_actions;
};
} // namespace Lighthouse
