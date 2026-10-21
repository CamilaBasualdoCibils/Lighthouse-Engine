#pragma once

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>

struct ImDrawData;

namespace Lighthouse {

enum class ImGuiPlatformBackend { Other, Vulkan };

// Implemented by render devices that can draw Dear ImGui into an acquired image.
class IImGuiDevice {
public:
    virtual ~IImGuiDevice() = default;
    virtual ImGuiPlatformBackend PlatformBackend() const noexcept { return ImGuiPlatformBackend::Other; }
    virtual void InitializeImGui(SwapchainHandle swapchain) = 0;
    virtual void ShutdownImGui() noexcept = 0;
    virtual void NewImGuiFrame() = 0;
    virtual void RenderImGui(SwapchainHandle swapchain, ImageViewHandle image, ImDrawData* drawData) = 0;
};

} // namespace Lighthouse
