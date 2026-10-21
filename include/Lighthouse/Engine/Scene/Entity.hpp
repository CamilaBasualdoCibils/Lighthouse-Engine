#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <entt/entt.hpp>

namespace Lighthouse {

class Entity {
public:
    Entity() = default;

    template <typename Component, typename... Args>
    Component& Add(Args&&... args) {
        auto registry = m_registry.lock();
        if (!registry || !registry->valid(m_entity)) {
            throw std::logic_error("Cannot add a component to an invalid entity");
        }
        return registry->emplace_or_replace<Component>(m_entity, std::forward<Args>(args)...);
    }

    const std::string& Name() const;
    bool IsValid() const;

private:
    friend class Scene;

    Entity(std::shared_ptr<entt::registry> registry, entt::entity entity, std::string name);

    std::weak_ptr<entt::registry> m_registry;
    entt::entity m_entity{entt::null};
    std::string m_name;
};

} // namespace Lighthouse
