#pragma once

#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>
#include <limits>
#include <stdexcept>

namespace Lighthouse
{
// Owned, tightly packed 2D pixels. Copying copies bytes; no device or GPU
// lifetime is involved. Decoders normalize their output to one explicit format.
class ImageData
{
public:
  ImageData(UVec2 resolution, ImageFormat format, std::vector<std::byte> data)
      : m_resolution(resolution), m_format(format), m_data(std::move(data))
  {
    const auto pixelSize = BytesPerPixel(format);
    if (!resolution.x || !resolution.y ||
        size_t(resolution.y) >
            std::numeric_limits<size_t>::max() / pixelSize / resolution.x ||
        m_data.size() != size_t(resolution.x) * resolution.y * pixelSize)
      throw std::invalid_argument(
          "ImageData resolution does not match its pixel bytes");
  }

  UVec2 GetResolution() const noexcept
  {
    return m_resolution;
  }
  ImageFormat GetFormat() const noexcept
  {
    return m_format;
  }
  std::span<const std::byte> GetData() const noexcept
  {
    return m_data;
  }

  // A borrowed upload description: keep this ImageData alive until the device
  // consumes initialData. This method does not allocate or upload GPU
  // resources.
  ImageDesc ToImageDesc(Flags<ImageUsage> usage = {ImageUsage::Sampled}) const
  {
    return {.width = m_resolution.x,
            .height = m_resolution.y,
            .format = m_format,
            .usage = usage,
            .initialData = m_data};
  }

private:
  static size_t BytesPerPixel(ImageFormat format)
  {
    switch (format)
    {
    case ImageFormat::R8:
      return 1;
    case ImageFormat::RG8:
    case ImageFormat::R16f:
    case ImageFormat::Depth16:
      return 2;
    case ImageFormat::RGB8:
      return 3;
    case ImageFormat::RGBA8:
    case ImageFormat::BGRA8:
    case ImageFormat::BGRA8Srgb:
    case ImageFormat::RG16f:
    case ImageFormat::R32f:
    case ImageFormat::Depth32:
    case ImageFormat::Depth24Stencil8:
      return 4;
    case ImageFormat::RGB16f:
      return 6;
    case ImageFormat::RGBA16f:
    case ImageFormat::RG32f:
      return 8;
    case ImageFormat::RGB32f:
      return 12;
    case ImageFormat::RGBA32f:
      return 16;
    default:
      throw std::invalid_argument("ImageData requires a defined pixel format");
    }
  }
  UVec2 m_resolution;
  ImageFormat m_format;
  std::vector<std::byte> m_data;
};
} // namespace Lighthouse
