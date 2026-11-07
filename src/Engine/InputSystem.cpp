#include <Lighthouse/Engine/Input/InputSystem.hpp>
#include <algorithm>
namespace Lighthouse {
std::shared_ptr<InputActionSet> InputSystem::CreateActionSet(std::string name) {
    for (const auto& set : m_actionSets) if (set->Name() == name)
        throw std::invalid_argument("Duplicate input action set name");
    auto set = std::make_shared<InputActionSet>(std::move(name));
    m_actionSets.push_back(set);
    return set;
}
void InputSystem::RemoveActionSet(const std::shared_ptr<InputActionSet>& set) {
    std::erase(m_actionSets, set);
}
void InputSystem::Update() {
    InputAction::States states;
    const auto sets = m_actionSets;
    for (const auto& set : sets) if (set->m_enabled)
        for (const auto& action : set->m_actions)
            for (const auto& binding : action->m_bindings)
                if (!states.contains(binding.device.get()))
                    states.emplace(binding.device.get(), binding.device->ReadState());
    std::vector<std::function<void()>> dispatch;
    for (const auto& set : sets)
        for (const auto& action : set->m_actions)
            action->Evaluate(states, set->m_enabled, dispatch);
    // Callbacks may change sets/bindings without invalidating evaluation.
    for (const auto& callback : dispatch) callback();
}

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
    m_actionSets.clear();
    for (auto it = m_backends.rbegin(); it != m_backends.rend(); ++it) (*it)->Shutdown();
    m_backends.clear();
}
} // namespace Lighthouse
