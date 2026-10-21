#pragma once

#include <string>

#include <Lighthouse/Engine/GraphicsBackend.hpp>
#include <Lighthouse/Engine/RendererBackend.hpp>
#include <Lighthouse/Engine/Types.hpp>

namespace Engine {

struct Config {
    std::string name;
    GraphicsConfig graphics;
    // Kept for source compatibility with the initial API scaffold.
    RendererBackend renderer{RendererBackend::Vulkan};
    Resolution resolution{};
    bool vsync{true};
};

} // namespace Engine
