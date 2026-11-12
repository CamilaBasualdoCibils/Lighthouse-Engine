#pragma once
#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Lighthouse
{
struct ImageMipData
{
  std::uint32_t width{1}, height{1}, depth{1};
  std::vector<std::byte> data;
};
// Owned, tightly packed pixels for authored mip levels.
struct ImageData
{
  ImageFormat format{ImageFormat::Undefined};
  std::vector<ImageMipData> mips;
  // Upload the base and generate the remaining levels on the GPU.
  bool generateMipmaps{false};

  ImageData() = default;
  ImageData(ImageFormat format, std::vector<ImageMipData> mips)
      : format(format), mips(std::move(mips)) { Validate(); }
  // Compatibility convenience for existing single-level image producers.
  ImageData(UVec2 resolution, ImageFormat format, std::vector<std::byte> data)
      : ImageData(format, {{resolution.x, resolution.y, 1, std::move(data)}}) {}

  void Validate() const
  {
    const auto pixelSize = BytesPerPixel(format);
    if (mips.empty()) throw std::invalid_argument("ImageData requires a base mip");
    for (std::size_t level = 0; level < mips.size(); ++level) {
      const auto& mip = mips[level];
      const auto maximum = std::numeric_limits<std::size_t>::max();
      if (!mip.width || !mip.height || !mip.depth ||
          std::size_t(mip.width) > maximum / pixelSize / mip.height / mip.depth ||
          mip.data.size() != std::size_t(mip.width) * mip.height * mip.depth * pixelSize)
        throw std::invalid_argument("Image mip dimensions do not match its pixel bytes");
      if (level) {
        const auto& previous = mips[level - 1];
        if ((previous.width == 1 && previous.height == 1 && previous.depth == 1) ||
            mip.width != std::max(1u, previous.width / 2) ||
            mip.height != std::max(1u, previous.height / 2) ||
            mip.depth != std::max(1u, previous.depth / 2))
          throw std::invalid_argument("Invalid image mip dimensions");
      }
    }
  }
  UVec2 GetResolution(std::size_t mip = 0) const
  { const auto& value = mips.at(mip); return {value.width, value.height}; }
  std::uint32_t GetDepth(std::size_t mip = 0) const { return mips.at(mip).depth; }
  std::uint32_t GetMipCount() const noexcept { return static_cast<std::uint32_t>(mips.size()); }
  std::uint32_t GetRequiredMipCount() const
  {
    if (!generateMipmaps) return GetMipCount();
    const auto& base = mips.at(0);
    auto size = std::max({base.width, base.height, base.depth});
    std::uint32_t count = 1;
    while (size > 1) { size /= 2; ++count; }
    return count;
  }
  ImageFormat GetFormat() const noexcept { return format; }
  std::span<const std::byte> GetData(std::size_t mip = 0) const { return mips.at(mip).data; }

  // Pixel spans are borrowed until CreateImage consumes them synchronously.
  ImageDesc ToImageDesc(Flags<ImageUsage> usage = {ImageUsage::Sampled}) const
  {
    Validate();
    ImageDesc result{.width=mips[0].width, .height=mips[0].height, .depth=mips[0].depth,
                     .format=format, .mipLevels=GetRequiredMipCount(), .usage=usage,
                     .type=mips[0].depth > 1 ? ImageType::Image3D : ImageType::Image2D};
    if (generateMipmaps) {
      result.usage |= ImageUsage::TransferSource;
      result.usage |= ImageUsage::TransferDestination;
      result.initialMips.emplace_back(mips.front().data);
    } else {
      for (const auto& mip : mips) result.initialMips.emplace_back(mip.data);
    }
    return result;
  }
  static std::size_t BytesPerPixel(ImageFormat format)
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
    case ImageFormat::RGBA8Srgb:
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
};
} // namespace Lighthouse
