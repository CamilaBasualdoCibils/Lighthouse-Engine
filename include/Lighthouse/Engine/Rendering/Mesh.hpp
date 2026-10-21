#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Rendering/RenderTypes.hpp>

namespace Lighthouse {

enum class IndexFormat : std::uint8_t { UInt16, UInt32 };

inline constexpr std::uint32_t AutoOffset = std::numeric_limits<std::uint32_t>::max();
std::uint32_t VertexFormatByteSize(VertexFormat format);
std::uint32_t IndexFormatByteSize(IndexFormat format);

template <std::ranges::contiguous_range Range>
    requires std::ranges::sized_range<Range> && std::is_trivially_copyable_v<std::ranges::range_value_t<Range>>
std::span<const std::byte> AsBytes(const Range& values) {
    using Value = std::ranges::range_value_t<Range>;
    return std::as_bytes(std::span<const Value>(std::ranges::data(values), std::ranges::size(values)));
}

struct MeshVertexBufferBinding {
    BufferHandle buffer;
    std::uint64_t offset{};
    std::uint32_t binding{};
};

struct MeshIndexBufferBinding {
    BufferHandle buffer;
    std::uint64_t offset{};
    IndexFormat format{IndexFormat::UInt32};
};

struct MeshVertexData {
    std::uint32_t binding{};
    std::vector<std::byte> bytes;
    MemoryDesc memory{};
};

struct MeshDesc {
    VertexLayout layout;
    std::vector<MeshVertexBufferBinding> vertexBuffers;
    std::optional<MeshIndexBufferBinding> indexBuffer;
    std::vector<MeshVertexData> vertexData;
    std::vector<std::byte> indexData;
    bool hasIndexData{};
    MemoryDesc indexMemory{};
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};
    std::uint32_t vertexCount{};
    std::uint32_t indexCount{};
    std::uint32_t instanceCount{1};
};

class MeshBuilder {
public:
    MeshBuilder& Attribute(std::string semantic, VertexFormat format, std::uint32_t location,
                           std::uint32_t binding = 0, std::uint32_t offset = AutoOffset);
    MeshBuilder& VertexBuffer(BufferHandle buffer, std::uint32_t binding = 0, std::uint64_t offset = 0);
    MeshBuilder& VertexStride(std::uint32_t stride, std::uint32_t binding = 0);
    MeshBuilder& InputRate(VertexInputRate rate, std::uint32_t binding = 0);
    MeshBuilder& IndexBuffer(BufferHandle buffer, IndexFormat format = IndexFormat::UInt32, std::uint64_t offset = 0);
    MeshBuilder& VertexData(std::span<const std::byte> bytes, std::uint32_t binding = 0,
                            MemoryDesc memory = {});
    MeshBuilder& IndexData(std::span<const std::byte> bytes, IndexFormat format = IndexFormat::UInt32,
                           MemoryDesc memory = {});

    template <typename T>
    MeshBuilder& VertexData(std::span<const T> values, std::uint32_t binding = 0, MemoryDesc memory = {}) {
        static_assert(std::is_trivially_copyable_v<T>);
        return VertexData(std::as_bytes(values), binding, memory);
    }
    template <typename T>
    MeshBuilder& IndexData(std::span<const T> values, IndexFormat format = IndexFormat::UInt32, MemoryDesc memory = {}) {
        static_assert(std::is_trivially_copyable_v<T>);
        return IndexData(std::as_bytes(values), format, memory);
    }

    MeshBuilder& Topology(PrimitiveTopology topology);
    MeshBuilder& VertexCount(std::uint32_t count);
    MeshBuilder& IndexCount(std::uint32_t count);
    MeshBuilder& InstanceCount(std::uint32_t count);
    MeshDesc Build() const;

private:
    struct AttributeSpec { std::string semantic; VertexFormat format; std::uint32_t location, binding, offset; };
    std::vector<AttributeSpec> m_attributes;
    std::vector<MeshVertexBufferBinding> m_vertexBuffers;
    std::vector<VertexBufferLayout> m_bindings;
    std::optional<MeshIndexBufferBinding> m_indexBuffer;
    std::vector<MeshVertexData> m_vertexData;
    std::vector<std::byte> m_indexData;
    bool m_hasIndexData{};
    MemoryDesc m_indexMemory{};
    PrimitiveTopology m_topology{PrimitiveTopology::TriangleList};
    std::uint32_t m_vertexCount{}, m_indexCount{}, m_instanceCount{1};
};

struct MeshHandleTag;
using MeshHandle = Handle<MeshHandleTag>;

enum class MeshUploadStatus : std::uint8_t { Invalid, Ready, UploadNotImplemented };

} // namespace Lighthouse
