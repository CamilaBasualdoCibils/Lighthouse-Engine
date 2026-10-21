#pragma once

namespace Engine {

struct Camera {
    float fieldOfView{};

    static Camera Perspective(float fieldOfViewDegrees);
};

} // namespace Engine
