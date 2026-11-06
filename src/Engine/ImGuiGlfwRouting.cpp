#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Rendering/Detail/ImGuiGlfwRouting.hpp>
#include <imgui.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <unordered_map>
namespace Lighthouse::Detail
{
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
  for (auto it = viewportCallbacks.begin(); it != viewportCallbacks.end();)
    if (it->second.context == context)
      it = viewportCallbacks.erase(it);
    else
      ++it;
}
} // namespace Lighthouse::Detail
#endif
