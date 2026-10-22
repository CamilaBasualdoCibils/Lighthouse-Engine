#include <Lighthouse/Engine/Rendering/RenderMesh.hpp>

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Lighthouse {

std::uint32_t VertexFormatByteSize(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float: case VertexFormat::Int: case VertexFormat::UInt: return 4;
    case VertexFormat::Float2: case VertexFormat::Int2: case VertexFormat::UInt2: return 8;
    case VertexFormat::Float3: case VertexFormat::Int3: case VertexFormat::UInt3: return 12;
    case VertexFormat::Float4: case VertexFormat::Int4: case VertexFormat::UInt4: return 16;
    case VertexFormat::UNorm8x4: case VertexFormat::SNorm8x4: return 4;
    }
    throw std::invalid_argument("Unknown vertex format");
}

std::uint32_t IndexFormatByteSize(IndexFormat format) {
    switch (format) {
    case IndexFormat::UInt16: return 2;
    case IndexFormat::UInt32: return 4;
    }
    throw std::invalid_argument("Unknown index format");
}

RenderMeshBuilder& RenderMeshBuilder::Attribute(std::string semantic, VertexFormat format, std::uint32_t location,
                                    std::uint32_t binding, std::uint32_t offset) {
    m_attributes.push_back({std::move(semantic), format, location, binding, offset});
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::VertexBuffer(BufferHandle buffer, std::uint32_t binding, std::uint64_t offset) {
    m_vertexBuffers.push_back({buffer, offset, binding});
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::VertexStride(std::uint32_t stride, std::uint32_t binding) {
    const auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [binding](const auto& item) { return item.binding == binding; });
    if (it == m_bindings.end()) m_bindings.push_back({binding, stride, VertexInputRate::PerVertex});
    else it->stride = stride;
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::InputRate(VertexInputRate rate, std::uint32_t binding) {
    const auto it = std::find_if(m_bindings.begin(), m_bindings.end(), [binding](const auto& item) { return item.binding == binding; });
    if (it == m_bindings.end()) m_bindings.push_back({binding, 0, rate});
    else it->inputRate = rate;
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::IndexBuffer(BufferHandle buffer, IndexFormat format, std::uint64_t offset) {
    if (m_indexBuffer) throw std::invalid_argument("A mesh can have only one index buffer");
    m_indexBuffer = RenderMeshIndexBufferBinding{buffer, offset, format};
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::VertexData(std::span<const std::byte> bytes, std::uint32_t binding, MemoryDesc memory) {
    const auto it = std::find_if(m_vertexData.begin(), m_vertexData.end(), [binding](const auto& item) { return item.binding == binding; });
    if (it != m_vertexData.end()) throw std::invalid_argument("Vertex data was already supplied for this binding");
    m_vertexData.push_back({binding, {bytes.begin(), bytes.end()}, memory});
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::IndexData(std::span<const std::byte> bytes, IndexFormat format, MemoryDesc memory) {
    if (m_indexBuffer) throw std::invalid_argument("CPU index data cannot be combined with an external index buffer");
    if (m_hasIndexData) throw std::invalid_argument("CPU index data was already supplied");
    m_indexData.assign(bytes.begin(), bytes.end());
    m_indexBuffer = RenderMeshIndexBufferBinding{{}, 0, format};
    m_indexMemory = memory;
    m_hasIndexData = true;
    return *this;
}

RenderMeshBuilder& RenderMeshBuilder::Topology(PrimitiveTopology topology) { m_topology = topology; return *this; }
RenderMeshBuilder& RenderMeshBuilder::VertexCount(std::uint32_t count) { m_vertexCount = count; return *this; }
RenderMeshBuilder& RenderMeshBuilder::IndexCount(std::uint32_t count) { m_indexCount = count; return *this; }
RenderMeshBuilder& RenderMeshBuilder::InstanceCount(std::uint32_t count) { m_instanceCount = count; return *this; }

RenderMesh RenderMeshBuilder::Build() const {
    RenderMesh result;
    result.vertexBuffers = m_vertexBuffers;
    result.indexBuffer = m_indexBuffer;
    result.vertexData = m_vertexData;
    result.indexData = m_indexData;
    result.hasIndexData = m_hasIndexData;
    result.indexMemory = m_indexMemory;
    result.topology = m_topology;
    result.vertexCount = m_vertexCount;
    result.indexCount = m_indexCount;
    result.instanceCount = m_instanceCount;

    std::unordered_set<std::uint32_t> locations;
    std::unordered_set<std::uint32_t> bindingIds;
    for (const auto& binding : m_bindings) {
        if (!bindingIds.insert(binding.binding).second) throw std::invalid_argument("Duplicate vertex layout binding");
        result.layout.bindings.push_back(binding);
    }
    std::unordered_map<std::uint32_t, std::uint32_t> extents;
    for (const auto& spec : m_attributes) {
        if (spec.semantic.empty()) throw std::invalid_argument("Vertex attribute semantic cannot be empty");
        if (!locations.insert(spec.location).second) throw std::invalid_argument("Duplicate vertex attribute location " + std::to_string(spec.location));
        const auto size = VertexFormatByteSize(spec.format);
        const auto current = extents[spec.binding];
        const auto offset = spec.offset == AutoOffset ? current : spec.offset;
        if (offset > std::numeric_limits<std::uint32_t>::max() - size)
            throw std::invalid_argument("Vertex attribute extent overflows 32-bit layout offsets");
        extents[spec.binding] = std::max(current, offset + size);
    result.layout.attributes.push_back({spec.location, spec.binding, spec.format, offset, spec.semantic});
    }

    for (const auto& [binding, extent] : extents) {
        auto it = std::find_if(result.layout.bindings.begin(), result.layout.bindings.end(),
            [binding](const auto& item) { return item.binding == binding; });
        if (it == result.layout.bindings.end()) result.layout.bindings.push_back({binding, extent, VertexInputRate::PerVertex});
        else if (it->stride == 0) it->stride = extent;
    }
    std::sort(result.layout.bindings.begin(), result.layout.bindings.end(),
              [](const auto& a, const auto& b) { return a.binding < b.binding; });
    for (const auto& attribute : result.layout.attributes) {
        const auto& binding = *std::find_if(result.layout.bindings.begin(), result.layout.bindings.end(),
            [&attribute](const auto& item) { return item.binding == attribute.binding; });
        const auto size = VertexFormatByteSize(attribute.format);
        if (attribute.offset > binding.stride || size > binding.stride - attribute.offset)
            throw std::invalid_argument("Vertex attribute '" + attribute.semantic + "' exceeds the explicitly configured stride");
    }
    return result;
}

} // namespace Lighthouse
