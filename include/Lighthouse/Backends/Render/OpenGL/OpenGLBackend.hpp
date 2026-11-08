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
    bool RequiresEGLWindow() const noexcept override { return true; }
    std::unique_ptr<IRenderDevice>
    CreateDevice(const RenderDeviceCreateInfo& config) override;
    void Shutdown() noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Lighthouse
