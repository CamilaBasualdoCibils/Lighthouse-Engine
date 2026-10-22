#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <string>
#include <vector>

#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>

namespace Lighthouse {

enum class QueueType : std::uint32_t { Graphics, Compute, Transfer };
enum class ImageFormat : std::uint32_t { Undefined, R8, RG8, RGBA8, RGBA8Srgb, Depth16, Depth24Stencil8, Depth32 };
enum class ImageType : std::uint32_t { Image1D, Image2D, Image3D, Image2DArray, Cube, CubeArray };
enum class ImageUsage : std::uint32_t {
    None = 0,
    Sampled = 1 << 0,
    Storage = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthAttachment = 1 << 3,
    TransferSource = 1 << 4,
    TransferDestination = 1 << 5
};
enum class BufferUsage : std::uint32_t {
    None = 0, Vertex = 1 << 0, Index = 1 << 1, Uniform = 1 << 2, Storage = 1 << 3,
    TransferSrc = 1 << 4, TransferDst = 1 << 5, Indirect = 1 << 6
};
enum class ShaderStageFlags : std::uint32_t { Vertex, Fragment, Compute };
enum class PrimitiveTopology : std::uint32_t {
    PointList, LineList, LineStrip, TriangleList, TriangleStrip,
    Points = PointList, Lines = LineList, Triangles = TriangleList
};
enum class VertexFormat : std::uint32_t {
    Float, Float2, Float3, Float4,
    Int, Int2, Int3, Int4,
    UInt, UInt2, UInt3, UInt4,
    UNorm8x4, SNorm8x4
};
enum class VertexInputRate : std::uint8_t { PerVertex, PerInstance };

enum class MemoryLocation : std::uint8_t { Auto, DeviceLocal, HostLocal, HostCached };
enum class MemoryAccess : std::uint32_t {
    None = 0, CPURead = 1u << 0, CPUWrite = 1u << 1, GPURead = 1u << 2, GPUWrite = 1u << 3
};
constexpr MemoryAccess operator|(MemoryAccess a, MemoryAccess b) noexcept {
    return static_cast<MemoryAccess>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr MemoryAccess operator&(MemoryAccess a, MemoryAccess b) noexcept {
    return static_cast<MemoryAccess>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr MemoryAccess& operator|=(MemoryAccess& a, MemoryAccess b) noexcept { return a = a | b; }
struct MemoryDesc {
    MemoryLocation preferredLocation{MemoryLocation::DeviceLocal};
    MemoryAccess requiredAccess{MemoryAccess::GPURead};
    MemoryAccess preferredAccess{MemoryAccess::None};
};

constexpr ImageUsage operator|(ImageUsage left, ImageUsage right) noexcept {
    return static_cast<ImageUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
constexpr BufferUsage operator&(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
constexpr BufferUsage& operator|=(BufferUsage& left, BufferUsage right) noexcept { return left = left | right; }

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

struct ImageDesc {
    std::uint32_t width{1};
    std::uint32_t height{1};
    std::uint32_t depth{1};
    ImageFormat format{ImageFormat::Undefined};
    std::uint32_t mipLevels{1};
    std::uint32_t arrayLayers{1};
    std::uint32_t sampleCount{1};
    ImageUsage usage{ImageUsage::Sampled};
    std::span<const std::byte> initialData{};
    ImageType type{ImageType::Image2D};
};

struct ImageViewDesc {
    ImageHandle image;
    ImageFormat format{ImageFormat::Undefined};
    ImageType type{ImageType::Image2D};
    std::uint32_t baseMipLevel{};
    std::uint32_t mipLevelCount{1};
    std::uint32_t baseArrayLayer{};
    std::uint32_t arrayLayerCount{1};
};

struct BufferDesc {
    std::uint64_t size{};
    BufferUsage usage{BufferUsage::None};
    std::span<const std::byte> initialData{};
    MemoryDesc memory{};
};

struct BufferMetadata {
    std::uint64_t size{};
    BufferUsage usage{BufferUsage::None};
    MemoryDesc memory{};
};

struct ShaderModuleDesc {
    ShaderStageFlags stage{ShaderStageFlags::Vertex};
    // Canonical Vulkan-targeted SPIR-V, provided as bytes for backend compatibility.
    std::span<const std::byte> bytecode{};
    std::string_view entryPoint{"main"};
};

struct VertexAttribute {
    std::uint32_t location{};
    std::uint32_t binding{};
    VertexFormat format{VertexFormat::Float};
    std::uint32_t offset{};
    std::string semantic;
};
struct VertexBinding {
    std::uint32_t binding{};
    std::uint32_t stride{};
    VertexInputRate inputRate{VertexInputRate::PerVertex};
};
using VertexBufferLayout = VertexBinding;
struct VertexLayout {
    std::vector<VertexBinding> bindings;
    std::vector<VertexAttribute> attributes;
};

struct GraphicsPipelineDesc {
    ShaderModuleHandle vertexShader;
    ShaderModuleHandle fragmentShader;
    VertexLayout vertexLayout;
    PrimitiveTopology topology{PrimitiveTopology::Triangles};
    std::vector<ImageFormat> colorFormats;
    ImageFormat depthFormat{ImageFormat::Undefined};
    std::uint32_t sampleCount{1};
};

struct RenderPassDesc {
    ImageViewHandle colorAttachment;
    ImageViewHandle depthAttachment;
    bool clearColor{};
    Vec3 clearColorValue{};
    bool clearDepth{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::int32_t offsetX{};
    std::int32_t offsetY{};
};

struct RenderCapabilities {
    bool multiView{};
    bool externalMemory{};
    bool externalSemaphore{};
    bool fragmentDensityMap{};
    bool variableRateShading{};
};

struct DeviceConfig {
    std::string_view applicationName{"Lighthouse"};
    bool enableValidation{};
};

struct SwapchainConfig {
    bool vsync{true};
    std::uint32_t imageCount{3};
};

} // namespace Lighthouse
