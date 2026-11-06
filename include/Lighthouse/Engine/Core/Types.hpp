#pragma once
#include <glm/detail/qualifier.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
namespace Lighthouse {

template <glm::length_t L, typename T> using Vec = glm::vec<L, T>;
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;
using Dvec2 = glm::dvec2;
using Dvec3 = glm::dvec3;
using Dvec4 = glm::dvec4;
using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;
using UVec2 = glm::uvec2;
using UVec3 = glm::uvec3;
using UVec4 = glm::uvec4;
using BVec2 = glm::bvec2;
using BVec3 = glm::bvec3;
using BVec4 = glm::bvec4;

using Mat2 = glm::mat2;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

using Quat = glm::quat;

using Resolution = UVec2;

} // namespace Lighthouse
