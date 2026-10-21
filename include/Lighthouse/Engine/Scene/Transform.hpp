#pragma once

#include <Lighthouse/Engine/Core/Types.hpp>

namespace Lighthouse {

struct Transform {
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Transform() = default;
    explicit Transform(Vec3 position);
};

} // namespace Lighthouse
