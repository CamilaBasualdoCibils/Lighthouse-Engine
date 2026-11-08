#pragma once
#include <Lighthouse/Engine/Input/InputAction.hpp>
#include <functional>
#include <stdexcept>
#include <typeindex>
#include <type_traits>
#include <vector>
#include <Lighthouse/Engine/Input/InputBackend.hpp>
namespace Lighthouse {
struct ConfiguredInputBackend {
    std::type_index type;
    std::function<std::unique_ptr<IInputBackend>()> factory;
};
struct InputConfig {
    std::vector<ConfiguredInputBackend> backends;
    template<class T> void AddBackend() {
        static_assert(std::is_base_of_v<IInputBackend, T>);
        for (const auto& backend : backends) if (backend.type == typeid(T)) return;
        backends.push_back({typeid(T), [] { return std::make_unique<T>(); }});
    }
};
class InputSystem {
public:
    std::shared_ptr<InputActionSet> CreateActionSet(std::string name);
    void RemoveActionSet(const std::shared_ptr<InputActionSet>& set);
    void Update();
    const auto& GetActionSets() const noexcept { return m_actionSets; }
    std::vector<IInputBackend*> GetBackends() const {
        std::vector<IInputBackend*> result;
        for (const auto& backend : m_backends) result.push_back(backend.get());
        return result;
    }
    ~InputSystem() { Shutdown(); }
    bool Initialize(const InputConfig& config, WindowManager& windows, std::string& diagnostic);
    void Shutdown() noexcept;
    template<class T> T& GetBackend() {
        for (auto& backend : m_backends)
            if (auto* result = dynamic_cast<T*>(backend.get())) return *result;
        throw std::out_of_range("Input backend is not configured or initialized");
    }
    template<class T> const T& GetBackend() const {
        for (const auto& backend : m_backends)
            if (auto* result = dynamic_cast<const T*>(backend.get())) return *result;
        throw std::out_of_range("Input backend is not configured or initialized");
    }
private:
    std::vector<std::shared_ptr<InputActionSet>> m_actionSets;
    std::vector<std::unique_ptr<IInputBackend>> m_backends;
};
} // namespace Lighthouse
