#include <Lighthouse/Engine/Window/WindowManager.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include <GLFW/glfw3native.h>
#include <wayland-egl.h>

#include <algorithm>
#include <iterator>
#include <exception>
#include <stdexcept>
#include <utility>

namespace Lighthouse {

Window::Window(WindowManager* owner, WindowHandle handle, const WindowDesc& desc)
    : m_owner(owner), m_handle(handle), m_title(desc.title), m_width(desc.width), m_height(desc.height),
      m_visible(desc.visible), m_mode(desc.mode) {}

void Window::Show() {
    m_owner->RequireMainThread();
    glfwShowWindow(m_native);
    m_visible = true;
}

void Window::Hide() {
    m_owner->RequireMainThread();
    glfwHideWindow(m_native);
    m_visible = false;
}

bool Window::ShouldClose() const {
    m_owner->RequireMainThread();
    return glfwWindowShouldClose(m_native) == GLFW_TRUE;
}

WindowManager::WindowManager() : m_mainThread(std::this_thread::get_id()) {}

WindowManager::~WindowManager() {
    Shutdown();
}

void WindowManager::Shutdown() noexcept {
    if (std::this_thread::get_id() != m_mainThread) std::terminate();
    for (auto& slot : m_slots) {
        if (slot.window && slot.window->m_native) {
            if (slot.window->m_eglWindow) wl_egl_window_destroy(static_cast<wl_egl_window*>(slot.window->m_eglWindow));
            glfwDestroyWindow(slot.window->m_native);
            slot.window.reset();
            if (++slot.generation == 0) ++slot.generation;
        }
    }
    if (m_glfwInitialized) glfwTerminate();
    m_glfwInitialized = false;
    m_monitors.clear();
    m_events.clear();
}

void WindowManager::RequireMainThread() const {
    if (std::this_thread::get_id() != m_mainThread) {
        throw std::logic_error("Native window operations must run on the WindowManager main thread");
    }
}

WindowHandle WindowManager::CreateWindow(const WindowDesc& desc) {
    RequireMainThread();
    if (desc.width == 0 || desc.height == 0) throw std::invalid_argument("Window dimensions must be nonzero");
    if (!m_glfwInitialized) {
        if (!glfwInit()) throw std::runtime_error("GLFW could not initialize a native window system");
        m_glfwInitialized = true;
        int count{};
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) m_monitors.push_back(monitors[i]);
    }
    std::uint32_t index = 0;
    while (index < m_slots.size() && m_slots[index].window) ++index;
    if (index == m_slots.size()) m_slots.emplace_back();
    auto& slot = m_slots[index];
    const WindowHandle handle{index, slot.generation};

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, desc.resizable ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, desc.visible ? GLFW_TRUE : GLFW_FALSE);
    GLFWmonitor* monitor = nullptr;
    int x = 0, y = 0;
    bool setPosition = false;
    auto width = static_cast<int>(desc.width);
    auto height = static_cast<int>(desc.height);
    if (desc.mode != WindowMode::Windowed) {
        monitor = glfwGetPrimaryMonitor();
        if (!monitor) throw std::runtime_error("No display is available for fullscreen window creation");
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode && desc.mode == WindowMode::BorderlessFullscreen) {
            width = mode->width;
            height = mode->height;
            glfwWindowHint(GLFW_RED_BITS, mode->redBits);
            glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
            glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
            glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        }
        if (desc.mode == WindowMode::BorderlessFullscreen) {
            glfwGetMonitorPos(monitor, &x, &y);
            monitor = nullptr;
            setPosition = true;
            glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        }
    }
    auto window = std::unique_ptr<Window>(new Window(this, handle, desc));
    window->m_native = glfwCreateWindow(width, height, desc.title.c_str(), monitor, nullptr);
    if (!window->m_native) throw std::runtime_error("GLFW failed to create a window");
    window->m_width = static_cast<std::uint32_t>(width);
    window->m_height = static_cast<std::uint32_t>(height);
    slot.window = std::move(window);
    auto* native = slot.window->m_native;
    if (desc.eglSurface) {
        auto* display = glfwGetWaylandDisplay();
        auto* surface = glfwGetWaylandWindow(native);
        if (!display || !surface) {
            glfwDestroyWindow(native);
            slot.window.reset();
            throw std::runtime_error("GLFW did not provide Wayland handles for the EGL window");
        }
        int pixelWidth{}, pixelHeight{};
        glfwGetFramebufferSize(native, &pixelWidth, &pixelHeight);
        auto* eglWindow = wl_egl_window_create(surface, pixelWidth, pixelHeight);
        if (!eglWindow) {
            glfwDestroyWindow(native);
            slot.window.reset();
            throw std::runtime_error("Failed to create the Wayland EGL window wrapper");
        }
        slot.window->m_nativeDisplay = display;
        slot.window->m_eglWindow = eglWindow;
    }
    glfwSetWindowUserPointer(native, slot.window.get());
    glfwSetWindowCloseCallback(native, &OnClose);
    glfwSetWindowSizeCallback(native, &OnSize);
    glfwSetFramebufferSizeCallback(native, &OnFramebufferSize);
    glfwSetWindowFocusCallback(native, &OnFocus);
    glfwSetWindowPosCallback(native, &OnPosition);
    if (setPosition) glfwSetWindowPos(native, x, y);
    glfwGetWindowPos(native, &slot.window->m_x, &slot.window->m_y);
    int framebufferWidth{}, framebufferHeight{};
    glfwGetFramebufferSize(native, &framebufferWidth, &framebufferHeight);
    slot.window->m_framebufferWidth = static_cast<std::uint32_t>(std::max(framebufferWidth, 0));
    slot.window->m_framebufferHeight = static_cast<std::uint32_t>(std::max(framebufferHeight, 0));
    slot.window->m_focused = glfwGetWindowAttrib(native, GLFW_FOCUSED) == GLFW_TRUE;
    return handle;
}

void WindowManager::DestroyWindow(WindowHandle handle) {
    RequireMainThread();
    auto* window = GetWindow(handle);
    if (!window) return;
    if (window->m_eglWindow) wl_egl_window_destroy(static_cast<wl_egl_window*>(window->m_eglWindow));
    glfwDestroyWindow(window->m_native);
    auto& slot = m_slots[handle.index];
    slot.window.reset();
    ++slot.generation;
    if (slot.generation == 0) ++slot.generation;
}

Window* WindowManager::GetWindow(WindowHandle handle) noexcept {
    if (!handle || handle.index >= m_slots.size()) return nullptr;
    auto& slot = m_slots[handle.index];
    return slot.generation == handle.generation ? slot.window.get() : nullptr;
}
const Window* WindowManager::GetWindow(WindowHandle handle) const noexcept {
    if (!handle || handle.index >= m_slots.size()) return nullptr;
    const auto& slot = m_slots[handle.index];
    return slot.generation == handle.generation ? slot.window.get() : nullptr;
}

void WindowManager::PollEvents() {
    RequireMainThread();
    if (!m_glfwInitialized) return;
    glfwPollEvents();
    int count{};
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    std::vector<void*> current;
    for (int i = 0; i < count; ++i) current.push_back(monitors[i]);
    if (current != m_monitors) {
        m_monitors = std::move(current);
        for (const auto& slot : m_slots) if (slot.window) QueueEvent({WindowEventType::DisplayChanged, slot.window->m_handle});
    }
}
std::vector<WindowEvent> WindowManager::DrainEvents() { RequireMainThread(); auto out = std::move(m_events); m_events.clear(); return out; }
bool WindowManager::ShouldClose() const {
    RequireMainThread();
    if (std::none_of(m_slots.begin(), m_slots.end(), [](const Slot& slot) { return static_cast<bool>(slot.window); })) {
        return false;
    }
    for (const auto& slot : m_slots) {
        if (!slot.window) continue;
        if (!slot.window->ShouldClose()) return false;
    }
    return true;
}
void WindowManager::QueueEvent(WindowEvent event) { m_events.push_back(event); }
Window* WindowManager::From(GLFWwindow* native) { return static_cast<Window*>(glfwGetWindowUserPointer(native)); }
void WindowManager::OnClose(GLFWwindow* native) {
    if (auto* w = From(native)) w->m_owner->QueueEvent({WindowEventType::Close, w->m_handle});
}
void WindowManager::OnSize(GLFWwindow* native, int width, int height) {
    if (auto* w = From(native)) { w->m_width = static_cast<std::uint32_t>(std::max(width, 0)); w->m_height = static_cast<std::uint32_t>(std::max(height, 0)); w->m_owner->QueueEvent({WindowEventType::Resize, w->m_handle, 0, 0, w->m_width, w->m_height}); }
}
void WindowManager::OnFramebufferSize(GLFWwindow* native, int width, int height) {
    if (auto* w = From(native)) {
        w->m_framebufferWidth = static_cast<std::uint32_t>(std::max(width, 0));
        w->m_framebufferHeight = static_cast<std::uint32_t>(std::max(height, 0));
        if (w->m_eglWindow) wl_egl_window_resize(static_cast<wl_egl_window*>(w->m_eglWindow), std::max(width, 1), std::max(height, 1), 0, 0);
    }
}
void WindowManager::OnFocus(GLFWwindow* native, int focused) {
    if (auto* w = From(native)) { w->m_focused = focused == GLFW_TRUE; WindowEvent e{WindowEventType::Focus, w->m_handle}; e.focused = w->m_focused; w->m_owner->QueueEvent(e); }
}
void WindowManager::OnPosition(GLFWwindow* native, int x, int y) {
    if (auto* w = From(native)) { w->m_x = x; w->m_y = y; w->m_owner->QueueEvent({WindowEventType::Move, w->m_handle, x, y}); }
}

} // namespace Lighthouse
