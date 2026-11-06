#include <Lighthouse/Engine/Core/EngineRuntime.hpp>
#include <spdlog/spdlog.h>

namespace Lighthouse
{

EngineRuntime::~EngineRuntime()
{
  Shutdown();
}

bool EngineRuntime::Initialize(const Config& config)
{
  static const BackendRegistry emptyRegistry;
  return Initialize(config, emptyRegistry);
}

bool EngineRuntime::Initialize(const Config& config,
                               const BackendRegistry& backends)
{
  Shutdown();
  const DeviceConfig deviceConfig{.applicationName = config.name};
  m_backend = backends.Select(config.graphics, &deviceConfig);
  for (const auto& attempt : m_backend.attempts){
    if (!attempt.selected)
      spdlog::warn("Graphics backend {}: {}", attempt.backend,
                   attempt.diagnostic);}
  if (!m_backend)
    return config.graphics.backends.empty();
  try
  {
    m_renderer =
        std::make_unique<Renderer>(std::move(m_backend.device), &m_windows);
    spdlog::info("Graphics backend: {} ({})", m_backend.selectedBackend,
                 m_renderer->GetRenderDevice()->GetCapabilities().deviceName);
    return true;
  }
  catch (const std::exception& error)
  {
    m_backend.attempts.push_back(
        {.backend = m_backend.selectedBackend, .diagnostic = error.what()});
    if (m_backend.backend)
      m_backend.backend->Shutdown();
    m_backend.backend.reset();
    return false;
  }
}

void EngineRuntime::Shutdown() noexcept
{
  m_renderer.reset();
  m_backend.device.reset();
  if (m_backend.backend)
    m_backend.backend->Shutdown();
  m_backend.backend.reset();
  m_windows.Shutdown();
}

} // namespace Lighthouse
