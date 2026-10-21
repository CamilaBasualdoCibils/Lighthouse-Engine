#pragma once

#include <memory>
#include <string>
#include <type_traits>
#include <chrono>
#include <exception>
#include <iostream>

#include <Lighthouse/Engine/Core/Config.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>
#include <Lighthouse/Engine/Core/EngineRuntime.hpp>

namespace Lighthouse {

class Application {
public:
    Application();
    virtual ~Application();

    virtual void OnStart() = 0;
    virtual void OnUpdate(float deltaTime) = 0;

    virtual bool ShouldClose() const;

protected:
    Scene& CreateScene(std::string name);
    EngineRuntime& GetRuntime();
    WindowManager& GetWindowManager();
    Renderer& GetRenderer();
    IGraphicsBackend* GetGraphicsBackend();
    const Config& GetConfig() const;

private:
    struct Storage;
    std::shared_ptr<Storage> m_storage;

    void AttachRuntime(EngineRuntime& runtime, const Config& config);
    template <typename ApplicationType>
    friend int Run(const Config& config);
};

template <typename ApplicationType>
int Run(const Config& config) {
    static_assert(std::is_base_of_v<Application, ApplicationType>,
                  "Run<T> requires T to derive from Lighthouse::Application");

    EngineRuntime runtime;
    if (!runtime.Initialize(config)) return 1;

    int result = 0;
    try {
        {
            ApplicationType application;
            application.AttachRuntime(runtime, config);
            application.OnStart();

            using Clock = std::chrono::steady_clock;
            auto previousFrame = Clock::now();
            while (!application.ShouldClose()) {
                runtime.GetWindowManager().PollEvents();
                const auto now = Clock::now();
                const auto deltaTime = std::chrono::duration<float>(now - previousFrame).count();
                previousFrame = now;
                application.OnUpdate(deltaTime);
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Lighthouse application failed: " << error.what() << '\n';
        result = 1;
    } catch (...) {
        std::cerr << "Lighthouse application failed with an unknown exception.\n";
        result = 1;
    }

    runtime.Shutdown();
    return result;
}

} // namespace Lighthouse
