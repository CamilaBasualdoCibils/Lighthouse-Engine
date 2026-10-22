#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>

namespace Lighthouse {

using ShaderStage = ShaderStageFlags;

class Shader {
public:
    Shader(ShaderStage stage, std::string entryPoint, std::vector<std::uint32_t> spirv);

    ShaderStage GetStage() const noexcept { return m_stage; }
    std::string_view GetEntryPoint() const noexcept { return m_entryPoint; }
    std::span<const std::uint32_t> GetSPIRV() const noexcept { return m_spirv; }

private:
    ShaderStage m_stage;
    std::string m_entryPoint;
    std::vector<std::uint32_t> m_spirv;
};

} // namespace Lighthouse
