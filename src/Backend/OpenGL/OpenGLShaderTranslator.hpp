#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>

namespace Lighthouse::Detail {

struct OpenGLBindingLimits {
    std::uint32_t uniformBuffers{};
    std::uint32_t storageBuffers{};
    std::uint32_t textureUnits{};
    std::uint32_t imageUnits{};
    std::uint32_t atomicCounterBuffers{};
};

std::string TranslateShaderToOpenGL(std::span<const std::uint32_t> spirv,
    ShaderStageFlags stage, std::string_view entryPoint, OpenGLBindingLimits limits);

} // namespace Lighthouse::Detail
