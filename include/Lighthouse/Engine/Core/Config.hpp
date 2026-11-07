#pragma once

#include <string>
#include <Lighthouse/Engine/Input/InputSystem.hpp>

#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>

namespace Lighthouse {

struct Config {
    std::string name;
    GraphicsConfig graphics;
    InputConfig input;
    Resolution resolution{};
    bool vsync{true};

    template <typename BackendType>
    void AddRenderBackend() { graphics.AddBackend<BackendType>(); }
    template <typename BackendType>
    void AddInputBackend() { input.AddBackend<BackendType>(); }
};

} // namespace Lighthouse
