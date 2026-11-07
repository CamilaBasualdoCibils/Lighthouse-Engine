#pragma once
#include <Lighthouse/Engine/Input/InputBackend.hpp>
namespace Lighthouse {
// All operations, including device state reads, must run on the initializing thread.
// GLFW exposes one logical keyboard/mouse, not individual physical devices.
class GLFWInputBackend final : public IInputBackend {
public:
    static constexpr std::string_view BackendName = "GLFW";
    GLFWInputBackend();
    ~GLFWInputBackend() override;
    GLFWInputBackend(const GLFWInputBackend&) = delete;
    GLFWInputBackend& operator=(const GLFWInputBackend&) = delete;
    std::string_view Name() const noexcept override { return BackendName; }
    bool Initialize(WindowManager& windows, std::string& diagnostic) override;
    void Shutdown() noexcept override;
    std::vector<InputDeviceInfo> Enumerate(InputDeviceType type) override;
    std::shared_ptr<IInputDevice> OpenDevice(InputDeviceID id, WindowHandle window = {}) override;
private:
    struct State;
    std::shared_ptr<State> m_state;
};
} // namespace Lighthouse
