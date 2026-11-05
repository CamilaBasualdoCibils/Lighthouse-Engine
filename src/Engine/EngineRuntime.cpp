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
            m_renderer = std::make_unique<Renderer>(std::move(device), &m_windows);
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
    m_renderer.reset();
    if (m_backend.backend) m_backend.backend->Shutdown();
    m_backend.backend.reset();
    m_windows.Shutdown();
}

} // namespace Lighthouse
