#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <Lighthouse/Engine/Core/Handle.hpp>

namespace Lighthouse {
class WindowManager;
enum class InputDeviceType { Keyboard, Mouse, Controller };
// Opaque, process-local UUID. IDs belong to one backend instance and lifetime.
struct InputDeviceID {
    std::uint64_t backend{}, device{};
    bool operator==(const InputDeviceID&) const = default;
};
struct InputDeviceInfo {
    InputDeviceID id;
    InputDeviceType type{};
    std::string name;
};
struct InputDeviceState {
    bool connected{};
    // Keyboard: USB HID keyboard usages. Mouse: left/right/middle then extra buttons.
    // Controller: backend-reported button and axis order (standard gamepad when mapped).
    std::vector<bool> buttons;
    std::vector<float> axes;
    double cursorX{}, cursorY{};
};
class IInputDevice {
public:
    virtual ~IInputDevice() = default;
    virtual const InputDeviceInfo& Info() const noexcept = 0;
    virtual InputDeviceState ReadState() const = 0;
};
class IInputBackend {
public:
    virtual ~IInputBackend() = default;
    virtual std::string_view Name() const noexcept = 0;
    virtual bool Initialize(WindowManager& windows, std::string& diagnostic) = 0;
    virtual void Shutdown() noexcept = 0;
    virtual std::vector<InputDeviceInfo> Enumerate(InputDeviceType type) = 0;
    // GLFW keyboard/mouse require an engine window. Invalid/foreign IDs throw.
    virtual std::shared_ptr<IInputDevice> OpenDevice(InputDeviceID id, WindowHandle window = {}) = 0;
    auto EnumerateKeyboards() { return Enumerate(InputDeviceType::Keyboard); }
    auto EnumerateMice() { return Enumerate(InputDeviceType::Mouse); }
    auto EnumerateControllers() { return Enumerate(InputDeviceType::Controller); }
};
} // namespace Lighthouse
