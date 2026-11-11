#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/Detail/ImGuiGlfwRouting.hpp>
#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#ifdef __linux__
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#undef None
#endif
#include <unordered_map>
namespace Lighthouse::Detail
{
#ifdef __linux__
// Wrap first-show only. Do not establish a transient parent or topmost state:
// users must be able to put a detached panel behind its former host later.
thread_local std::unordered_map<ImGuiContext*, void (*)(ImGuiViewport*)>
    viewportShowCallbacks;

void ShowDetachedViewport(ImGuiViewport* viewport)
{
  viewportShowCallbacks.at(ImGui::GetCurrentContext())(viewport);
  auto* native = static_cast<GLFWwindow*>(viewport->PlatformHandle);
  if (native)
  {
    auto* display = glfwGetX11Display();
    const auto window = glfwGetX11Window(native);
    XRaiseWindow(display, window);
    // Reparenting window managers own the outer frame, so ask the WM to
    // restack the client too. This does not request focus or set a sticky state.
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_RESTACK_WINDOW", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; // Application request.
    event.xclient.data.l[2] = Above;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
  }
}
#endif

void EnableImGuiGlfwViewportRaiseOnShow()
{
#ifdef __linux__
  if (glfwGetPlatform() != GLFW_PLATFORM_X11) return;
  auto* context = ImGui::GetCurrentContext();
  if (viewportShowCallbacks.contains(context)) return;
  auto& platform = ImGui::GetPlatformIO();
  viewportShowCallbacks.emplace(context, platform.Platform_ShowWindow);
  platform.Platform_ShowWindow = ShowDetachedViewport;
#endif
}

// GLFW dispatches events outside ImGuiView's frame scope. The installed
// backend's viewport move/resize/close callbacks assume a current context.
// Route those callbacks to their owning UI context, not an engine window.
struct ViewportCallbacks
{
  ImGuiContext* context{};
  GLFWwindowposfun position{};
  GLFWwindowsizefun size{};
  GLFWwindowclosefun close{};
};
thread_local std::unordered_map<GLFWwindow*, ViewportCallbacks>
    viewportCallbacks;
void ImGuiWindowPosition(GLFWwindow* window, int x, int y)
{
  const auto callbacks = viewportCallbacks.at(window);
  const auto previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(callbacks.context);
  if (callbacks.position)
    callbacks.position(window, x, y);
  ImGui::SetCurrentContext(previous);
}
void ImGuiWindowSize(GLFWwindow* window, int width, int height)
{
  const auto callbacks = viewportCallbacks.at(window);
  const auto previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(callbacks.context);
  if (callbacks.size)
    callbacks.size(window, width, height);
  ImGui::SetCurrentContext(previous);
}
void ImGuiWindowClose(GLFWwindow* window)
{
  const auto callbacks = viewportCallbacks.at(window);
  const auto previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(callbacks.context);
  if (callbacks.close)
    callbacks.close(window);
  ImGui::SetCurrentContext(previous);
}
void RouteImGuiGlfwCallbacks()
{
  const auto context = ImGui::GetCurrentContext();
  const auto& viewports = ImGui::GetPlatformIO().Viewports;
  for (int i = 1; i < viewports.Size; ++i)
  {
    auto* window = static_cast<GLFWwindow*>(viewports[i]->PlatformHandle);
    if (!window)
      continue;
    auto& callbacks = viewportCallbacks[window];
    callbacks.context = context;
    const auto position = glfwSetWindowPosCallback(window, ImGuiWindowPosition);
    const auto size = glfwSetWindowSizeCallback(window, ImGuiWindowSize);
    const auto close = glfwSetWindowCloseCallback(window, ImGuiWindowClose);
    if (position != ImGuiWindowPosition)
      callbacks.position = position;
    if (size != ImGuiWindowSize)
      callbacks.size = size;
    if (close != ImGuiWindowClose)
      callbacks.close = close;
  }
  for (auto it = viewportCallbacks.begin(); it != viewportCallbacks.end();)
  {
    bool live = it->second.context != context;
    for (int i = 1; !live && i < viewports.Size; ++i)
      live = viewports[i]->PlatformHandle == it->first;
    if (live)
      ++it;
    else
      it = viewportCallbacks.erase(it);
  }
}

void RemoveImGuiGlfwCallbacks(ImGuiContext* context)
{
#ifdef __linux__
  if (auto show = viewportShowCallbacks.find(context); show != viewportShowCallbacks.end())
  {
    if (ImGui::GetCurrentContext() == context)
      ImGui::GetPlatformIO().Platform_ShowWindow = show->second;
    viewportShowCallbacks.erase(show);
  }
#endif
  for (auto it = viewportCallbacks.begin(); it != viewportCallbacks.end();)
    if (it->second.context == context)
      it = viewportCallbacks.erase(it);
    else
      ++it;
}
} // namespace Lighthouse::Detail
#endif
