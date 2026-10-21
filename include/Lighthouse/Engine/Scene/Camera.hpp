#pragma once

namespace Lighthouse {

struct Camera {
    float fieldOfView{};

    static Camera Perspective(float fieldOfViewDegrees);
};

} // namespace Lighthouse
