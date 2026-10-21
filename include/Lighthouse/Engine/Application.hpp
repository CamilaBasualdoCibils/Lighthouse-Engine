#pragma once

#include <memory>
#include <string>

#include <Lighthouse/Engine/Config.hpp>
#include <Lighthouse/Engine/Scene.hpp>

namespace Engine {

class Application {
public:
    virtual ~Application() = default;

    virtual void OnStart() = 0;
    virtual void OnUpdate(float deltaTime) = 0;

protected:
    Scene& CreateScene(std::string name);

private:
    struct Storage;
    std::shared_ptr<Storage> m_storage;
};

template <typename ApplicationType>
int Run(const Config& config);

} // namespace Engine
