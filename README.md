# Lighthouse-Engine

Low-level graphics, render graph composition, resources, synchronization, windowing, application lifecycle, and input abstractions. Public types use namespace `Lighthouse`.

Clone recursively and configure with the `vcpkg-preset` preset. Enable `BUILD_TESTING` for CTest tests and `LIGHTHOUSE_BUILD_EXAMPLES` for the window example. Existing OpenGL, Vulkan, GLFW input, ImGui, and Tracy options remain available.

```sh
cmake --preset vcpkg-preset -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Consumers use `add_subdirectory(extern/Lighthouse-Engine)` and link `Lighthouse::Engine`, plus the backend targets they select. No Extended or EESG source, startup code, or resources are required.

The former low-level `Renderer` is now `RenderGraphComposer`; include `<Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>`. `IApplication::GetRenderer()` and `EngineRuntime::GetRenderer()` retain their accessor names and return the composer. Application lifecycle, RenderGraph architecture, and GPU synchronization are unchanged. CPU asset types (`GLTFAsset`, `Mesh`, `ImageData`) and `Assets::LoadGLTF()` belong to Engine. Mesh uploads accept either `composer.UploadMesh(mesh)` or a `RenderMesh` description.

## Embedded resources

All files under this repository's optional `res/` tree are compiled into the separate static target `Lighthouse::EngineEmbedded`, linked through Engine. This checkout currently has no Engine-owned `res/` files; the generator also supports an empty resource tree. New files are detected on the next build.

Include `<Lighthouse/Engine/EmbeddedResources.hpp>` and call `Lighthouse::EngineEmbedded::Find(relativePath)` for an optional immutable byte span or `ReadText(relativePath)` for text. Missing paths return `nullopt` or throw, respectively. Bytes, including embedded NULs and empty resources, have executable lifetime. No runtime source-resource directory is needed. VISR's existing shader embedding remains owned by VISR.
