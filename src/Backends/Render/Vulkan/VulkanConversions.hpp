#pragma once
#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>
#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <vulkan/vulkan.hpp>
namespace Lighthouse
{
namespace VulkanDetail
{
template <class T> struct Hash
{
  std::size_t operator()(T h) const noexcept
  {
    return h.index;
  }
};
template <class H, class T> using Map = std::unordered_map<H, T, Hash<H>>;
inline void Require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
// VMA is a C library. Native C handles/results are confined to these allocation
// calls (and the GLFW/ImGui adapters); all Vulkan rendering uses Vulkan-Hpp.
template <class Result> void AllocationCheck(Result rawResult)
{
  const auto result = static_cast<vk::Result>(rawResult);
  if (result != vk::Result::eSuccess)
    throw std::runtime_error("VMA allocation/mapping failed: " +
                             vk::to_string(result));
}
inline vk::Format Format(ImageFormat f)
{
  switch (f)
  {
#define F(a, b)                                                                \
  case ImageFormat::a:                                                         \
    return vk::Format::b
    F(R8, eR8Unorm);
    F(RG8, eR8G8Unorm);
    F(RGB8, eR8G8B8Unorm);
    F(RGBA8, eR8G8B8A8Unorm);
    F(RGB10A2, eA2B10G10R10UnormPack32);
    F(BGR10A2, eA2R10G10B10UnormPack32);
    F(BGRA8, eB8G8R8A8Unorm);
    F(BGRA8Srgb, eB8G8R8A8Srgb);
    F(RGBA8Srgb, eR8G8B8A8Srgb);
    F(R16f, eR16Sfloat);
    F(RG16f, eR16G16Sfloat);
    F(RGB16f, eR16G16B16Sfloat);
    F(RGBA16f, eR16G16B16A16Sfloat);
    F(R32f, eR32Sfloat);
    F(RG32f, eR32G32Sfloat);
    F(RGB32f, eR32G32B32Sfloat);
    F(RGBA32f, eR32G32B32A32Sfloat);
    F(Depth16, eD16Unorm);
    F(Depth24Stencil8, eD24UnormS8Uint);
    F(Depth32, eD32Sfloat);
#undef F
  default:
    throw std::invalid_argument("Undefined Vulkan image format");
  }
}
inline ImageFormat EngineFormat(vk::Format f)
{
  if (f == vk::Format::eR8G8B8A8Unorm)
    return ImageFormat::RGBA8;
  if (f == vk::Format::eR8G8B8A8Srgb)
    return ImageFormat::RGBA8Srgb;
  if (f == vk::Format::eB8G8R8A8Unorm)
    return ImageFormat::BGRA8;
  if (f == vk::Format::eB8G8R8A8Srgb)
    return ImageFormat::BGRA8Srgb;
  if (f == vk::Format::eR16G16B16A16Sfloat)
    return ImageFormat::RGBA16f;
  if (f == vk::Format::eA2R10G10B10UnormPack32)
    return ImageFormat::BGR10A2;
  if (f == vk::Format::eA2B10G10R10UnormPack32)
    return ImageFormat::RGB10A2;
  throw std::runtime_error("Unsupported presentation format");
}
inline std::optional<vk::SurfaceFormatKHR>
SelectSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& available,
                    SwapchainFormat requested)
{
  vk::ColorSpaceKHR colorSpace;
  std::vector<vk::Format> candidates;
  switch (requested)
  {
  case SwapchainFormat::SRGB:
    colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    // Preserve the existing display-encoded UNORM presentation contract.
    candidates = {vk::Format::eR8G8B8A8Unorm, vk::Format::eB8G8R8A8Unorm,
                  vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb};
    break;
  case SwapchainFormat::HDR10:
    colorSpace = vk::ColorSpaceKHR::eHdr10St2084EXT;
    candidates = {vk::Format::eA2B10G10R10UnormPack32,
                  vk::Format::eA2R10G10B10UnormPack32};
    break;
  case SwapchainFormat::ScRGB:
    colorSpace = vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;
    candidates = {vk::Format::eR16G16B16A16Sfloat};
    break;
  default:
    return std::nullopt;
  }
  for (auto format : candidates)
    for (const auto& supported : available)
      if (supported.colorSpace == colorSpace &&
          (supported.format == format ||
           supported.format == vk::Format::eUndefined))
        return vk::SurfaceFormatKHR(format, colorSpace);
  return std::nullopt;
}

inline vk::ImageAspectFlags Aspect(ImageFormat f)
{
  if (f == ImageFormat::Depth24Stencil8)
    return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
  if (f == ImageFormat::Depth16 || f == ImageFormat::Depth32)
    return vk::ImageAspectFlagBits::eDepth;
  return vk::ImageAspectFlagBits::eColor;
}
inline vk::ImageAspectFlags Aspect(Flags<ImageAspect> a)
{
  vk::ImageAspectFlags result;
  if (a.Contains(ImageAspect::Color))
    result |= vk::ImageAspectFlagBits::eColor;
  if (a.Contains(ImageAspect::Depth))
    result |= vk::ImageAspectFlagBits::eDepth;
  if (a.Contains(ImageAspect::Stencil))
    result |= vk::ImageAspectFlagBits::eStencil;
  return result;
}
inline vk::ShaderStageFlags Stages(ShaderStageFlags s)
{
  vk::ShaderStageFlags flags;
  if (static_cast<unsigned>(s) & 1)
    flags |= vk::ShaderStageFlagBits::eVertex;
  if (static_cast<unsigned>(s) & 2)
    flags |= vk::ShaderStageFlagBits::eFragment;
  if (static_cast<unsigned>(s) & 4)
    flags |= vk::ShaderStageFlagBits::eCompute;
  return flags;
}
inline vk::ShaderStageFlagBits Stage(ShaderStageFlags s)
{
  if (s == ShaderStageFlags::Vertex)
    return vk::ShaderStageFlagBits::eVertex;
  if (s == ShaderStageFlags::Fragment)
    return vk::ShaderStageFlagBits::eFragment;
  if (s == ShaderStageFlags::Compute)
    return vk::ShaderStageFlagBits::eCompute;
  throw std::invalid_argument("Shader module must have one stage");
}
inline vk::DescriptorType Descriptor(DescriptorType t)
{
  switch (t)
  {
  case DescriptorType::UniformBuffer:
    return vk::DescriptorType::eUniformBuffer;
  case DescriptorType::StorageBuffer:
    return vk::DescriptorType::eStorageBuffer;
  case DescriptorType::CombinedImageSampler:
    return vk::DescriptorType::eCombinedImageSampler;
  }
  throw std::invalid_argument("Unknown descriptor type");
}
inline vk::BlendFactor BlendFactorNative(BlendFactor value)
{
  switch (value)
  {
  case BlendFactor::Zero:
    return vk::BlendFactor::eZero;
  case BlendFactor::One:
    return vk::BlendFactor::eOne;
  case BlendFactor::SourceColor:
    return vk::BlendFactor::eSrcColor;
  case BlendFactor::OneMinusSourceColor:
    return vk::BlendFactor::eOneMinusSrcColor;
  case BlendFactor::DestinationColor:
    return vk::BlendFactor::eDstColor;
  case BlendFactor::OneMinusDestinationColor:
    return vk::BlendFactor::eOneMinusDstColor;
  case BlendFactor::SourceAlpha:
    return vk::BlendFactor::eSrcAlpha;
  case BlendFactor::OneMinusSourceAlpha:
    return vk::BlendFactor::eOneMinusSrcAlpha;
  case BlendFactor::DestinationAlpha:
    return vk::BlendFactor::eDstAlpha;
  case BlendFactor::OneMinusDestinationAlpha:
    return vk::BlendFactor::eOneMinusDstAlpha;
  case BlendFactor::ConstantColor:
    return vk::BlendFactor::eConstantColor;
  case BlendFactor::OneMinusConstantColor:
    return vk::BlendFactor::eOneMinusConstantColor;
  case BlendFactor::ConstantAlpha:
    return vk::BlendFactor::eConstantAlpha;
  case BlendFactor::OneMinusConstantAlpha:
    return vk::BlendFactor::eOneMinusConstantAlpha;
  case BlendFactor::SourceAlphaSaturate:
    return vk::BlendFactor::eSrcAlphaSaturate;
  }
  throw std::invalid_argument("Unsupported BlendFactor");
}

inline vk::BlendOp BlendOperationNative(BlendOperation value)
{
  switch (value)
  {
  case BlendOperation::Add:
    return vk::BlendOp::eAdd;
  case BlendOperation::Subtract:
    return vk::BlendOp::eSubtract;
  case BlendOperation::ReverseSubtract:
    return vk::BlendOp::eReverseSubtract;
  case BlendOperation::Min:
    return vk::BlendOp::eMin;
  case BlendOperation::Max:
    return vk::BlendOp::eMax;
  }
  throw std::invalid_argument("Unsupported BlendOperation");
}

inline vk::PrimitiveTopology Topology(PrimitiveTopology t)
{
  switch (t)
  {
  case PrimitiveTopology::TriangleList:
    return vk::PrimitiveTopology::eTriangleList;
  case PrimitiveTopology::TriangleStrip:
    return vk::PrimitiveTopology::eTriangleStrip;
  case PrimitiveTopology::PointList:
    return vk::PrimitiveTopology::ePointList;
  case PrimitiveTopology::LineList:
    return vk::PrimitiveTopology::eLineList;
  case PrimitiveTopology::LineStrip:
    return vk::PrimitiveTopology::eLineStrip;
  }
  throw std::invalid_argument("Unsupported topology");
}
inline vk::Format VertexFormatNative(VertexFormat f)
{
  switch (f)
  {
#define F(a, b)                                                                \
  case VertexFormat::a:                                                        \
    return vk::Format::b
    F(Float, eR32Sfloat);
    F(Float2, eR32G32Sfloat);
    F(Float3, eR32G32B32Sfloat);
    F(Float4, eR32G32B32A32Sfloat);
    F(Int, eR32Sint);
    F(Int2, eR32G32Sint);
    F(Int3, eR32G32B32Sint);
    F(Int4, eR32G32B32A32Sint);
    F(UInt, eR32Uint);
    F(UInt2, eR32G32Uint);
    F(UInt3, eR32G32B32Uint);
    F(UInt4, eR32G32B32A32Uint);
    F(UNorm8x4, eR8G8B8A8Unorm);
    F(SNorm8x4, eR8G8B8A8Snorm);
#undef F
  }
  throw std::invalid_argument("Unsupported vertex format");
}
inline vk::SampleCountFlagBits Samples(unsigned n)
{
  switch (n)
  {
  case 1:
    return vk::SampleCountFlagBits::e1;
  case 2:
    return vk::SampleCountFlagBits::e2;
  case 4:
    return vk::SampleCountFlagBits::e4;
  case 8:
    return vk::SampleCountFlagBits::e8;
  default:
    throw std::invalid_argument("Unsupported sample count");
  }
}
} // namespace VulkanDetail

} // namespace Lighthouse
