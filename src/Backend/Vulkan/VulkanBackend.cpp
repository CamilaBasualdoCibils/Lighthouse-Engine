#include <Lighthouse/Vulkan/VulkanBackend.hpp>
#include <Lighthouse/Vulkan/VulkanRenderDevice.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

#include <stdexcept>

namespace Lighthouse {

bool VulkanBackend::Initialize(std::string& diagnostic) {
    diagnostic.clear();
    return true;
}

GraphicsCapabilities VulkanBackend::Capabilities() const { return {}; }
std::unique_ptr<IRenderDevice> VulkanBackend::CreateDevice(const DeviceConfig& config) {
    return std::make_unique<VulkanRenderDevice>(config);
}
void VulkanBackend::Shutdown() noexcept {}

} // namespace Lighthouse
