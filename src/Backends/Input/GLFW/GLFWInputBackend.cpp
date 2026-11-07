#include <Lighthouse/Backends/Input/GLFW/GLFWInputBackend.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <thread>

namespace Lighthouse {
namespace {
std::atomic_uint64_t nextBackend{1};
class Device final : public IInputDevice {
public:
    Device(InputDeviceInfo info, std::function<InputDeviceState()> read)
        : m_info(std::move(info)), m_read(std::move(read)) {}
    const InputDeviceInfo& Info() const noexcept override { return m_info; }
    InputDeviceState ReadState() const override { return m_read(); }
private:
    InputDeviceInfo m_info;
    std::function<InputDeviceState()> m_read;
};
// Convert GLFW's portable key tokens to USB HID keyboard usages.
int KeyUsage(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z) return 4 + key - GLFW_KEY_A;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) return 30 + key - GLFW_KEY_1;
    if (key == GLFW_KEY_0) return 39;
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12) return 58 + key - GLFW_KEY_F1;
    if (key >= GLFW_KEY_F13 && key <= GLFW_KEY_F24) return 104 + key - GLFW_KEY_F13;
    if (key >= GLFW_KEY_KP_1 && key <= GLFW_KEY_KP_9) return 89 + key - GLFW_KEY_KP_1;
    switch (key) {
    case GLFW_KEY_ENTER: return 40; case GLFW_KEY_ESCAPE: return 41;
    case GLFW_KEY_BACKSPACE: return 42; case GLFW_KEY_TAB: return 43;
    case GLFW_KEY_SPACE: return 44; case GLFW_KEY_MINUS: return 45;
    case GLFW_KEY_EQUAL: return 46; case GLFW_KEY_LEFT_BRACKET: return 47;
    case GLFW_KEY_RIGHT_BRACKET: return 48; case GLFW_KEY_BACKSLASH: return 49;
    case GLFW_KEY_SEMICOLON: return 51; case GLFW_KEY_APOSTROPHE: return 52;
    case GLFW_KEY_GRAVE_ACCENT: return 53; case GLFW_KEY_COMMA: return 54;
    case GLFW_KEY_PERIOD: return 55; case GLFW_KEY_SLASH: return 56;
    case GLFW_KEY_CAPS_LOCK: return 57; case GLFW_KEY_PRINT_SCREEN: return 70;
    case GLFW_KEY_SCROLL_LOCK: return 71; case GLFW_KEY_PAUSE: return 72;
    case GLFW_KEY_INSERT: return 73; case GLFW_KEY_HOME: return 74;
    case GLFW_KEY_PAGE_UP: return 75; case GLFW_KEY_DELETE: return 76;
    case GLFW_KEY_END: return 77; case GLFW_KEY_PAGE_DOWN: return 78;
    case GLFW_KEY_RIGHT: return 79; case GLFW_KEY_LEFT: return 80;
    case GLFW_KEY_DOWN: return 81; case GLFW_KEY_UP: return 82;
    case GLFW_KEY_NUM_LOCK: return 83; case GLFW_KEY_KP_DIVIDE: return 84;
    case GLFW_KEY_KP_MULTIPLY: return 85; case GLFW_KEY_KP_SUBTRACT: return 86;
    case GLFW_KEY_KP_ADD: return 87; case GLFW_KEY_KP_ENTER: return 88;
    case GLFW_KEY_KP_0: return 98; case GLFW_KEY_KP_DECIMAL: return 99;
    case GLFW_KEY_MENU: return 101; case GLFW_KEY_KP_EQUAL: return 103;
    case GLFW_KEY_LEFT_CONTROL: return 224; case GLFW_KEY_LEFT_SHIFT: return 225;
    case GLFW_KEY_LEFT_ALT: return 226; case GLFW_KEY_LEFT_SUPER: return 227;
    case GLFW_KEY_RIGHT_CONTROL: return 228; case GLFW_KEY_RIGHT_SHIFT: return 229;
    case GLFW_KEY_RIGHT_ALT: return 230; case GLFW_KEY_RIGHT_SUPER: return 231;
    default: return -1;
    }
}
}
struct GLFWInputBackend::State {
    WindowManager* windows{};
    std::thread::id thread;
    std::uint64_t id{};
    void RequireThread() const {
        if (thread != std::this_thread::get_id())
            throw std::logic_error("GLFW input operations require the initializing thread");
    }
};
GLFWInputBackend::GLFWInputBackend() : m_state(std::make_shared<State>()) {}
GLFWInputBackend::~GLFWInputBackend() { Shutdown(); }
bool GLFWInputBackend::Initialize(WindowManager& windows, std::string& diagnostic) {
    Shutdown();
    try {
        m_state = std::make_shared<State>();
        windows.Initialize();
        m_state->thread = std::this_thread::get_id();
        m_state->id = nextBackend.fetch_add(1);
        m_state->windows = &windows;
        diagnostic.clear();
        return true;
    } catch (const std::exception& error) { diagnostic = error.what(); return false; }
}
void GLFWInputBackend::Shutdown() noexcept {
    // Open device handles retain this inactive state, never a dangling WindowManager.
    m_state->windows = nullptr;
}
std::vector<InputDeviceInfo> GLFWInputBackend::Enumerate(InputDeviceType type) {
    auto& state = *m_state;
    if (!state.windows) throw std::logic_error("GLFW input backend is not initialized");
    state.RequireThread();
    if (type == InputDeviceType::Keyboard) return {{{state.id, 1}, type, "GLFW logical keyboard"}};
    if (type == InputDeviceType::Mouse) return {{{state.id, 2}, type, "GLFW logical mouse"}};
    std::vector<InputDeviceInfo> result;
    if (type == InputDeviceType::Controller) {
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (!glfwJoystickPresent(jid)) continue;
            const char* name = glfwJoystickIsGamepad(jid) ? glfwGetGamepadName(jid) : glfwGetJoystickName(jid);
            result.push_back({{state.id, static_cast<std::uint64_t>(jid + 3)}, type, name ? name : "Controller"});
        }
    }
    return result;
}
std::shared_ptr<IInputDevice> GLFWInputBackend::OpenDevice(InputDeviceID id, WindowHandle window) {
    if (!m_state->windows) throw std::logic_error("GLFW input backend is not initialized");
    m_state->RequireThread();
    if (id.backend != m_state->id || id.device < 1 || id.device > GLFW_JOYSTICK_LAST + 3)
        throw std::invalid_argument("Input device ID does not belong to this GLFW backend");
    const auto type = id.device == 1 ? InputDeviceType::Keyboard : id.device == 2 ? InputDeviceType::Mouse : InputDeviceType::Controller;
    InputDeviceInfo info;
    bool found = false;
    for (const auto& candidate : Enumerate(type)) if (candidate.id == id) { info = candidate; found = true; break; }
    if (!found) throw std::invalid_argument("Input device is disconnected");
    if (type != InputDeviceType::Controller && !m_state->windows->GetWindow(window))
        throw std::invalid_argument("GLFW keyboard/mouse require a valid engine window");
    return std::make_shared<Device>(std::move(info), [state = m_state, id, type, window] {
        InputDeviceState result;
        if (!state->windows) return result;
        state->RequireThread();
        if (type == InputDeviceType::Controller) {
            const int jid = static_cast<int>(id.device - 3);
            if (!glfwJoystickPresent(jid)) return result;
            result.connected = true;
            GLFWgamepadstate gamepad{};
            if (glfwGetGamepadState(jid, &gamepad)) {
                for (auto button : gamepad.buttons) result.buttons.push_back(button == GLFW_PRESS);
                result.axes.assign(std::begin(gamepad.axes), std::end(gamepad.axes));
            } else {
                int count = 0;
                const auto* buttons = glfwGetJoystickButtons(jid, &count);
                for (int i = 0; i < count; ++i) result.buttons.push_back(buttons[i] == GLFW_PRESS);
                const auto* axes = glfwGetJoystickAxes(jid, &count);
                if (axes && count) result.axes.assign(axes, axes + count);
            }
            return result;
        }
        auto* target = state->windows->GetWindow(window);
        if (!target) return result;
        auto* native = static_cast<GLFWwindow*>(target->NativeHandle());
        result.connected = true;
        if (type == InputDeviceType::Keyboard) {
            result.buttons.resize(256);
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
                const int usage = KeyUsage(key);
                if (usage >= 0) result.buttons[usage] = glfwGetKey(native, key) == GLFW_PRESS;
            }
        } else {
            result.buttons.resize(GLFW_MOUSE_BUTTON_LAST + 1);
            for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button)
                result.buttons[button] = glfwGetMouseButton(native, button) == GLFW_PRESS;
            glfwGetCursorPos(native, &result.cursorX, &result.cursorY);
        }
        return result;
    });
}
} // namespace Lighthouse
