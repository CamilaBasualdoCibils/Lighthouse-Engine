#include <Lighthouse/Engine/Input/InputSystem.hpp>
namespace Lighthouse {
bool InputSystem::Initialize(const InputConfig& config, WindowManager& windows, std::string& diagnostic) {
    Shutdown();
    try {
        for (const auto& entry : config.backends) {
            auto backend = entry.factory();
            if (!backend) throw std::runtime_error("Input backend factory returned null");
            m_backends.push_back(std::move(backend));
            if (!m_backends.back()->Initialize(windows, diagnostic)) {
                Shutdown(); return false;
            }
        }
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what(); Shutdown(); return false;
    }
}
void InputSystem::Shutdown() noexcept {
    for (auto it = m_backends.rbegin(); it != m_backends.rend(); ++it) (*it)->Shutdown();
    m_backends.clear();
}
} // namespace Lighthouse
