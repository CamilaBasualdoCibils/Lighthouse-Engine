#pragma once

#include "Lighthouse/Engine/Core/Flags.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>

namespace Lighthouse
{

enum class QueueType : std::uint32_t
{
  Graphics,
  Compute,
  Transfer
};
enum class ImageFormat : std::uint32_t
{
  Undefined,
  R8,
  RG8,
  RGB8,
  RGBA8,

  R16f,
  RG16f,
  RGB16f,
  RGBA16f,

  R32f,
  RG32f,
  RGB32f,
  RGBA32f,

  BGRA8,
  BGRA8Srgb,
  RGBA8Srgb,
  Depth16,
  Depth24Stencil8,
  Depth32
};
enum class ImageType : std::uint32_t
{
  Image1D,
  Image2D,
  Image3D,
  Image2DArray,
  Cube,
  CubeArray
};
enum class ImageUsage : std::uint32_t
{
  Sampled = 1 << 0,
  Storage = 1 << 1,
  ColorAttachment = 1 << 2,
  DepthAttachment = 1 << 3,
  DepthStencilAttachment = 1 << 4,
  TransferSource = 1 << 5,
  TransferDestination = 1 << 6
};
enum class ImageAspect : std::uint32_t
{
  Color = 1 << 0,
  Depth = 1 << 1,
  Stencil = 1 << 2
};
enum class ImageFilter : std::uint32_t
{
  Nearest,
  Linear
};

enum class BufferUsage : std::uint32_t
{
  Vertex = 1 << 0,
  Index = 1 << 1,
  Uniform = 1 << 2,
  Storage = 1 << 3,
  TransferSrc = 1 << 4,
  TransferDst = 1 << 5,
  Indirect = 1 << 6
};
enum class ShaderStageFlags : std::uint32_t
{
  Vertex = 1u << 0,
  Fragment = 1u << 1,
  Compute = 1u << 2
};
constexpr ShaderStageFlags operator|(ShaderStageFlags left,
                                     ShaderStageFlags right) noexcept
{
  return static_cast<ShaderStageFlags>(static_cast<std::uint32_t>(left) |
                                       static_cast<std::uint32_t>(right));
}
constexpr ShaderStageFlags operator&(ShaderStageFlags left,
                                     ShaderStageFlags right) noexcept
{
  return static_cast<ShaderStageFlags>(static_cast<std::uint32_t>(left) &
                                       static_cast<std::uint32_t>(right));
}
enum class PrimitiveTopology : std::uint32_t
{
  PointList,
  LineList,
  LineStrip,
  TriangleList,
  TriangleStrip,
  Points = PointList,
  Lines = LineList,
  Triangles = TriangleList
};
enum class VertexFormat : std::uint32_t
{
  Float,
  Float2,
  Float3,
  Float4,
  Int,
  Int2,
  Int3,
  Int4,
  UInt,
  UInt2,
  UInt3,
  UInt4,
  UNorm8x4,
  SNorm8x4
};
enum class VertexInputRate : std::uint8_t
{
  PerVertex,
  PerInstance
};

enum class MemoryLocation : std::uint8_t
{
  Auto,
  DeviceLocal,
  HostLocal,
  HostCached
};
enum class MemoryAccess : std::uint32_t
{
  None = 0,
  CPURead = 1u << 0,
  CPUWrite = 1u << 1,
  GPURead = 1u << 2,
  GPUWrite = 1u << 3
};
constexpr MemoryAccess operator|(MemoryAccess a, MemoryAccess b) noexcept
{
  return static_cast<MemoryAccess>(static_cast<std::uint32_t>(a) |
                                   static_cast<std::uint32_t>(b));
}
constexpr MemoryAccess operator&(MemoryAccess a, MemoryAccess b) noexcept
{
  return static_cast<MemoryAccess>(static_cast<std::uint32_t>(a) &
                                   static_cast<std::uint32_t>(b));
}
constexpr MemoryAccess& operator|=(MemoryAccess& a, MemoryAccess b) noexcept
{
  return a = a | b;
}
struct MemoryDesc
{
  MemoryLocation preferredLocation{MemoryLocation::DeviceLocal};
  MemoryAccess requiredAccess{MemoryAccess::GPURead};
  MemoryAccess preferredAccess{MemoryAccess::None};
};

constexpr ImageUsage operator|(ImageUsage left, ImageUsage right) noexcept
{
  return static_cast<ImageUsage>(static_cast<std::uint32_t>(left) |
                                 static_cast<std::uint32_t>(right));
}
constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept
{
  return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) |
                                  static_cast<std::uint32_t>(right));
}
constexpr BufferUsage operator&(BufferUsage left, BufferUsage right) noexcept
{
  return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) &
                                  static_cast<std::uint32_t>(right));
}
constexpr BufferUsage& operator|=(BufferUsage& left, BufferUsage right) noexcept
{
  return left = left | right;
}

struct ImageTag;
struct ImageViewTag;
struct BufferTag;
struct ShaderModuleTag;
struct GraphicsPipelineTag;
using ImageHandle = Handle<ImageTag>;
using ImageViewHandle = Handle<ImageViewTag>;
using BufferHandle = Handle<BufferTag>;
using ShaderModuleHandle = Handle<ShaderModuleTag>;
using GraphicsPipelineHandle = Handle<GraphicsPipelineTag>;
using SwapchainHandle = Handle<SwapchainHandleTag>;
using DescriptorSetLayoutHandle = Handle<DescriptorSetLayoutTag>;
using DescriptorSetHandle = Handle<DescriptorSetTag>;
using PipelineLayoutHandle = Handle<PipelineLayoutTag>;
using SamplerHandle = Handle<SamplerTag>;

enum class DescriptorType : std::uint32_t
{
  UniformBuffer,
  StorageBuffer,
  CombinedImageSampler
};

struct DescriptorBindingDesc
{
  std::uint32_t binding{};
  DescriptorType type{DescriptorType::UniformBuffer};
  std::uint32_t count{1};
  ShaderStageFlags stages{ShaderStageFlags::Vertex};
};

struct DescriptorSetLayoutDesc
{
  std::vector<DescriptorBindingDesc> bindings;
};

struct DescriptorSetDesc
{
  DescriptorSetLayoutHandle layout;
};

inline constexpr std::uint64_t WholeBuffer =
    std::numeric_limits<std::uint64_t>::max();

struct DescriptorBufferInfo
{
  BufferHandle buffer;
  std::uint64_t offset{};
  std::uint64_t range{WholeBuffer};
};

struct DescriptorImageInfo
{
  ImageViewHandle image;
  SamplerHandle sampler;
};

using DescriptorResource =
    std::variant<DescriptorBufferInfo, DescriptorImageInfo>;

struct DescriptorWrite
{
  std::uint32_t binding{};
  std::uint32_t arrayElement{};
  DescriptorResource resource;
};

enum class SamplerFilter : std::uint32_t
{
  Nearest,
  Linear
};
enum class SamplerMipmapFilter : std::uint32_t
{
  None,
  Nearest,
  Linear
};
enum class SamplerAddressMode : std::uint32_t
{
  Repeat,
  MirroredRepeat,
  ClampToEdge,
  ClampToBorder
};

struct SamplerDesc
{
  SamplerFilter minification{SamplerFilter::Linear};
  SamplerFilter magnification{SamplerFilter::Linear};
  SamplerMipmapFilter mipmap{SamplerMipmapFilter::None};
  SamplerAddressMode addressU{SamplerAddressMode::Repeat};
  SamplerAddressMode addressV{SamplerAddressMode::Repeat};
  SamplerAddressMode addressW{SamplerAddressMode::Repeat};
  bool anisotropy{};
  float maxAnisotropy{1.0f};
};

struct PipelineLayoutDesc
{
  std::vector<DescriptorSetLayoutHandle> sets;
};

struct PipelineLayoutLocation
{
  DescriptorType type{};
  std::uint32_t bindingPoint{};
  std::uint32_t count{};
  ShaderStageFlags stages{ShaderStageFlags::Vertex |
                          ShaderStageFlags::Fragment |
                          ShaderStageFlags::Compute};
};

using DescriptorBindingLocations =
    std::map<std::pair<std::uint32_t, std::uint32_t>, PipelineLayoutLocation>;

struct ImageDesc
{
  std::uint32_t width{1};
  std::uint32_t height{1};
  std::uint32_t depth{1};
  ImageFormat format{ImageFormat::Undefined};
  std::uint32_t mipLevels{1};
  std::uint32_t arrayLayers{1};
  std::uint32_t sampleCount{1};
  Flags<ImageUsage> usage{ImageUsage::Sampled};
  std::span<const std::byte> initialData{};
  ImageType type{ImageType::Image2D};
};

struct ImageViewDesc
{
  ImageHandle image;
  ImageFormat format{ImageFormat::Undefined};
  ImageType type{ImageType::Image2D};
  std::uint32_t baseMipLevel{};
  std::uint32_t mipLevelCount{1};
  std::uint32_t baseArrayLayer{};
  std::uint32_t arrayLayerCount{1};
};
struct ImageSubresourceRange
{
  Flags<ImageAspect> aspectMask{ImageAspect::Color};
  uint32_t baseMipLevel{0};
  uint32_t mipLevelCount{1};
  uint32_t baseArrayLayer{0};
  uint32_t layerCount{1};
};
struct ImageSubresourceLayers
{
  Flags<ImageAspect> aspectMask{ImageAspect::Color};
  uint32_t mipLevel{0};
  uint32_t baseArrayLayer{0};
  uint32_t layerCount{1};
};

struct BlitRegion
{
  ImageSubresourceLayers srcSubresource;
  std::array<UVec3, 2> srcOffsets;

  ImageSubresourceLayers dstSubresource;
  std::array<UVec3, 2> dstOffsets;
};
struct BufferDesc
{
  std::uint64_t size{};
  Flags<BufferUsage> usage{};
  std::span<const std::byte> initialData{};
  MemoryDesc memory{};
};

struct BufferMetadata
{
  std::uint64_t size{};
  Flags<BufferUsage> usage{};
  MemoryDesc memory{};
};

struct ShaderModuleDesc
{
  ShaderStageFlags stage{ShaderStageFlags::Vertex};
  // Canonical Vulkan-targeted SPIR-V, provided as bytes for backend
  // compatibility.
  std::span<const std::byte> bytecode{};
  std::string_view entryPoint{"main"};
};

struct VertexAttribute
{
  std::uint32_t location{};
  std::uint32_t binding{};
  VertexFormat format{VertexFormat::Float};
  std::uint32_t offset{};
  std::string semantic;
};
struct VertexBinding
{
  std::uint32_t binding{};
  std::uint32_t stride{};
  VertexInputRate inputRate{VertexInputRate::PerVertex};
};
using VertexBufferLayout = VertexBinding;
struct VertexLayout
{
  std::vector<VertexBinding> bindings;
  std::vector<VertexAttribute> attributes;
};

struct GraphicsPipelineDesc
{
  ShaderModuleHandle vertexShader;
  ShaderModuleHandle fragmentShader;
  VertexLayout vertexLayout;
  PrimitiveTopology topology{PrimitiveTopology::Triangles};
  std::vector<ImageFormat> colorFormats;
  ImageFormat depthFormat{ImageFormat::Undefined};
  std::uint32_t sampleCount{1};
  PipelineLayoutHandle layout;
  ShaderModuleHandle computeShader;
};

struct RenderPassDesc
{
  ImageViewHandle colorAttachment;
  ImageViewHandle depthAttachment;
  bool clearColor{};
  Vec3 clearColorValue{};
  bool clearDepth{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::int32_t offsetX{};
  std::int32_t offsetY{};
  std::uint32_t viewMask{};
  std::uint32_t layerCount{1};
};

struct RenderCapabilities
{
  std::string deviceName;
  std::uint32_t
      apiVersion{}; // major << 22 | minor << 12 | patch (Vulkan encoding).
  bool headless{};
  bool computeShaders{};
  bool bindlessResources{};
  bool supportedBindlessResources{};
  bool supportedMeshShaders{}; // Enabled descriptor indexing / native bindless
                               // access.
  bool hardwareRayTracing{};   // Physical device support, not a ray-tracing API
                               // promise.
  bool rayTracing{}; // Enabled backend ray-tracing commands (currently
                     // unavailable).
  bool meshShaders{};
  bool asyncCompute{}; // False for the single-queue implementation.
  bool dynamicRendering{};
  bool shaderObjects{};
  bool imgui{};
  bool supportedMultiview{};
  bool multiView{}; // Usable by this backend, not merely supported by hardware.
  bool externalMemory{};
  bool externalSemaphore{};
  bool fragmentDensityMap{};
  bool variableRateShading{};
};

struct DeviceConfig
{
  std::string_view applicationName{"Lighthouse"};
  bool enableValidation{};
};

// Borrowed handles for the currently acquired swapchain image. The swapchain
// owns these resources; callers must not destroy them.
struct AcquiredSwapchainImage
{
  ImageHandle image;
  ImageViewHandle view;
};

struct SwapchainConfig
{
  bool vsync{true};
  std::uint32_t minImageCount{3};
};

} // namespace Lighthouse
