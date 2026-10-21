#include <Lighthouse/Engine/Core/EngineRuntime.hpp>

namespace Lighthouse {

EngineRuntime::~EngineRuntime() { Shutdown(); }

bool EngineRuntime::Initialize(const Config& config) {
    static const BackendRegistry emptyRegistry;
    return Initialize(config, emptyRegistry);
}

bool EngineRuntime::Initialize(const Config& config, const BackendRegistry& backends) {
    Shutdown();
    m_backend = backends.Select(config.graphics);
    if (m_backend.backend) {
        try {
            auto device = m_backend.backend->CreateDevice(DeviceConfig{.applicationName = config.name});
            if (!device) { Shutdown(); return false; }
            m_renderer.SetRenderDevice(std::move(device));
        } catch (...) {
            Shutdown();
            return false;
        }
    }
    // A missing graphics implementation is non-fatal; windowing and CPU-side
    // view configuration remain available without one.
    return true;
}

void EngineRuntime::Shutdown() noexcept {
    m_renderer.ResetRenderDevice();
    if (m_backend.backend) m_backend.backend->Shutdown();
    m_backend.backend.reset();
    m_windows.Shutdown();
}

} // namespace Lighthouse
