#pragma once

#include <string>
#include <string_view>
#include <memory>

#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>

namespace Lighthouse {

class OpenGLBackend final : public IGraphicsBackend {
public:
    OpenGLBackend();
    ~OpenGLBackend() override;
    static constexpr std::string_view BackendName = "OpenGL";
    std::string_view Name() const noexcept override { return BackendName; }
    bool Initialize(std::string& diagnostic) override;
    GraphicsCapabilities Capabilities() const override;
    std::unique_ptr<IRenderDevice> CreateDevice(const DeviceConfig& config) override;
    void Shutdown() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
