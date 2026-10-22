#include <Lighthouse/Engine/Assets/Shader.hpp>

#include <stdexcept>
#include <utility>

namespace Lighthouse {

Shader::Shader(ShaderStage stage, std::string entryPoint, std::vector<std::uint32_t> spirv)
    : m_stage(stage), m_entryPoint(std::move(entryPoint)), m_spirv(std::move(spirv)) {
    if (m_entryPoint.empty())
        throw std::invalid_argument("Shader entry point cannot be empty");
    if (m_spirv.empty() || m_spirv.front() != 0x07230203u)
        throw std::invalid_argument("Shader bytecode is not valid SPIR-V");
}

} // namespace Lighthouse
