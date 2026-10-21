#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Lighthouse/Engine/Handle.hpp>

struct GLFWwindow;

namespace Engine {

enum class WindowMode { Windowed, BorderlessFullscreen, Fullscreen };
enum class WindowEventType { Close, Resize, Focus, Move, DisplayChanged };

struct WindowDesc {
    std::string title;
    std::uint32_t width{1280};
    std::uint32_t height{720};
    WindowMode mode{WindowMode::Windowed};
    bool resizable{true};
    bool visible{true};
};

struct WindowEvent {
    WindowEventType type{};
    WindowHandle window;
    std::int32_t x{};
    std::int32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
    bool focused{};
};

class WindowManager;

class Window {
public:
    WindowHandle Handle() const noexcept { return m_handle; }
    const std::string& Title() const noexcept { return m_title; }
    std::uint32_t Width() const noexcept { return m_width; }
    std::uint32_t Height() const noexcept { return m_height; }
    std::uint32_t FramebufferWidth() const noexcept { return m_framebufferWidth; }
    std::uint32_t FramebufferHeight() const noexcept { return m_framebufferHeight; }
    std::int32_t X() const noexcept { return m_x; }
    std::int32_t Y() const noexcept { return m_y; }
    bool IsVisible() const noexcept { return m_visible; }
    bool IsFocused() const noexcept { return m_focused; }
    bool ShouldClose() const;
    WindowMode Mode() const noexcept { return m_mode; }
    void* NativeHandle() const noexcept { return m_native; }

    void Show();
    void Hide();

private:
    friend class WindowManager;
    Window(WindowManager* owner, WindowHandle handle, const WindowDesc& desc);
    WindowManager* m_owner{};
    WindowHandle m_handle;
    std::string m_title;
    std::uint32_t m_width{}, m_height{}, m_framebufferWidth{}, m_framebufferHeight{};
    std::int32_t m_x{}, m_y{};
    bool m_visible{}, m_focused{};
    WindowMode m_mode{};
    GLFWwindow* m_native{};
};

class WindowManager {
public:
    // Construct, use, and destroy the manager on the application's main thread.
    WindowManager();
    ~WindowManager();
    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    WindowHandle CreateWindow(const WindowDesc& desc);
    void DestroyWindow(WindowHandle window);
    void Shutdown() noexcept;
    Window* GetWindow(WindowHandle window) noexcept;
    const Window* GetWindow(WindowHandle window) const noexcept;
    void PollEvents();
    std::vector<WindowEvent> DrainEvents();
    bool IsAvailable() const noexcept { return m_glfwInitialized; }

private:
    friend class Window;
    struct Slot { std::uint32_t generation{1}; std::unique_ptr<Window> window; };
    void RequireMainThread() const;
    void QueueEvent(WindowEvent event);
    static Window* From(GLFWwindow* native);
    static void OnClose(GLFWwindow* native);
    static void OnSize(GLFWwindow* native, int width, int height);
    static void OnFramebufferSize(GLFWwindow* native, int width, int height);
    static void OnFocus(GLFWwindow* native, int focused);
    static void OnPosition(GLFWwindow* native, int x, int y);

    std::thread::id m_mainThread;
    std::vector<Slot> m_slots;
    std::vector<WindowEvent> m_events;
    bool m_glfwInitialized{};
    std::vector<void*> m_monitors;
};

} // namespace Engine
