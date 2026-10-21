#pragma once

#include <string>
#include <string_view>

#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>

namespace Lighthouse {

class VulkanBackend final : public IGraphicsBackend {
public:
    static constexpr std::string_view BackendName = "Vulkan";
    std::string_view Name() const noexcept override { return BackendName; }
    bool Initialize(std::string& diagnostic) override;
    GraphicsCapabilities Capabilities() const override;
    std::unique_ptr<IRenderDevice> CreateDevice(const DeviceConfig& config) override;
    void Shutdown() noexcept override;
};

} // namespace Lighthouse
