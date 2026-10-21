#pragma once

#include <string>

#include <Lighthouse/Engine/Graphics/GraphicsBackend.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>

namespace Lighthouse {

struct Config {
    std::string name;
    GraphicsConfig graphics;
    Resolution resolution{};
    bool vsync{true};

    template <typename BackendType>
    void AddBackend() { graphics.AddBackend<BackendType>(); }
};

} // namespace Lighthouse
