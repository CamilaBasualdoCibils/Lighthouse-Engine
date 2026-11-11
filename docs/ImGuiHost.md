# ImGui host window

Use `RenderWindowDesc::ImGuiHost` on the one engine window that should host
ImGui. It defaults to `false`.

```cpp
const auto host = composer.CreateWindowView({
    .title = "Editor",
    .width = 1280,
    .height = 720,
    .ImGuiHost = true,
});
composer.CreateView<ImGuiView>([] { ImGui::ShowDemoWindow(); });
composer.Compile();
```

The composer uses this existing GLFW window as ImGui's main viewport and sets
`ConfigViewportsNoAutoMerge` to false. The backend renders UI over the completed
scene before engine presentation. On X11, newly shown detached panels are raised once. After that they follow
normal window stacking: there is no transient-parent or always-on-top state.
Other engine
windows are never registered and cannot receive merged panels. Docking layouts
still require an application dockspace.

With no host, ImGui keeps its private hidden main viewport, auto-merge is disabled,
and visible panels use external windows. Existing `ImGuiViewDesc` demo options
work in both modes.

Only one host window and one hosted `ImGuiView` are allowed per composer.
External-only mode continues to support independent UI views. The host window
remains engine-owned, including its swapchain, presentation and destruction.
The ImGui GLFW backend chains existing input/focus callbacks; application input
should respect `WantCaptureMouse` and `WantCaptureKeyboard`.

Recompile after creating/removing views. Removing the host shuts down its ImGui
integration before destroying the native window and invalidates the graph. On
recompilation, UI returns to external-only mode unless another host was created.
Changing hosts recreates the UI contexts and resets transient UI state.

OpenGL hosted UI and detached panels share the engine's EGL resources, with
renderer callbacks managing detached-window EGL surfaces. The overlay currently
waits for scene submissions before loading color, adding a per-frame GPU
synchronization point. Vulkan uses the host swapchain attachment format and a
synchronous dynamic-rendering overlay submission. Both backends preserve scene
color and support host resize and UI-only hosts.
