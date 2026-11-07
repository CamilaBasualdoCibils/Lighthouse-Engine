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
  std::string inputDiagnostic;
  if (!m_input.Initialize(config.input, m_windows, inputDiagnostic)) {
    spdlog::error("Input initialization failed: {}", inputDiagnostic);
    Shutdown();
    return false;
  }
  const DeviceConfig deviceConfig{.applicationName = config.name};
  m_backend = backends.Select(config.graphics, &deviceConfig);
  for (const auto& attempt : m_backend.attempts){
    if (!attempt.selected)
      spdlog::warn("Graphics backend {}: {}", attempt.backend,
                   attempt.diagnostic);}
  if (!m_backend) {
    if (config.graphics.backends.empty()) return true;
    Shutdown(); return false;
  }
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
    Shutdown();
    return false;
  }
}

void EngineRuntime::Shutdown() noexcept
{
  m_input.Shutdown();
  m_renderer.reset();
  m_backend.device.reset();
  if (m_backend.backend)
    m_backend.backend->Shutdown();
  m_backend.backend.reset();
  m_windows.Shutdown();
}

} // namespace Lighthouse
