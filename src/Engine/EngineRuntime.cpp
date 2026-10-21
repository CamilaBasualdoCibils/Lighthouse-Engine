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
    // A missing graphics implementation is non-fatal; windowing and CPU-side
    // view/session configuration remain available without one.
    return true;
}

void EngineRuntime::Shutdown() noexcept {
    if (m_backend.backend) m_backend.backend->Shutdown();
    m_backend.backend.reset();
    m_windows.Shutdown();
}

} // namespace Lighthouse
