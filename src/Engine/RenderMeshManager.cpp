#include "RenderMeshManager.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace Lighthouse {
namespace {

bool HasUsage(BufferUsage value, BufferUsage required) {
    return (value & required) == required;
}

std::uint64_t RequiredBytes(std::uint64_t offset, std::uint64_t count, std::uint64_t stride,
                            const char* description) {
    if (stride && count > (std::numeric_limits<std::uint64_t>::max() - offset) / stride)
        throw std::invalid_argument(std::string(description) + " byte size overflows 64-bit range");
    return offset + count * stride;
}

void ValidateRenderMeshription(const RenderMesh& desc, IRenderDevice* device) {
    const bool hasBufferReferences = !desc.vertexBuffers.empty() ||
        (desc.indexBuffer && static_cast<bool>(desc.indexBuffer->buffer));
    if (hasBufferReferences && !device)
        throw std::logic_error("Attach the owning render device before creating a mesh that references device buffers");

    std::unordered_map<std::uint32_t, const VertexBinding*> layouts;
    for (const auto& binding : desc.layout.bindings) {
        if (!layouts.emplace(binding.binding, &binding).second)
            throw std::invalid_argument("Mesh layout contains duplicate binding " + std::to_string(binding.binding));
    }
    std::unordered_set<std::uint32_t> locations;
    for (const auto& attribute : desc.layout.attributes) {
        if (attribute.semantic.empty()) throw std::invalid_argument("Mesh attribute semantic cannot be empty");
        if (!locations.insert(attribute.location).second)
            throw std::invalid_argument("Mesh layout contains duplicate attribute location " + std::to_string(attribute.location));
        const auto layout = layouts.find(attribute.binding);
        if (layout == layouts.end())
            throw std::invalid_argument("Attribute '" + attribute.semantic + "' refers to missing vertex binding " + std::to_string(attribute.binding));
        const auto size = VertexFormatByteSize(attribute.format);
        if (attribute.offset > layout->second->stride || size > layout->second->stride - attribute.offset)
            throw std::invalid_argument("Attribute '" + attribute.semantic + "' exceeds the stride of binding " + std::to_string(attribute.binding));
    }
    switch (desc.topology) {
    case PrimitiveTopology::PointList: case PrimitiveTopology::LineList: case PrimitiveTopology::LineStrip:
    case PrimitiveTopology::TriangleList: case PrimitiveTopology::TriangleStrip: break;
    default: throw std::invalid_argument("Mesh has an unsupported primitive topology");
    }

    std::unordered_map<std::uint32_t, const RenderMeshVertexBufferBinding*> vertexBuffers;
    for (const auto& binding : desc.vertexBuffers) {
        if (!binding.buffer) throw std::invalid_argument("Mesh vertex binding " + std::to_string(binding.binding) + " has an invalid buffer handle");
        if (!vertexBuffers.emplace(binding.binding, &binding).second)
            throw std::invalid_argument("Mesh contains duplicate vertex buffer binding " + std::to_string(binding.binding));
        if (!device->IsValid(binding.buffer)) throw std::invalid_argument("Mesh references an unknown or stale vertex buffer");
        const auto metadata = device->GetBufferDesc(binding.buffer);
        if (!metadata) throw std::invalid_argument("Render device has no metadata for the referenced vertex buffer");
        if (!metadata->usage.Contains(BufferUsage::Vertex))
            throw std::invalid_argument("Referenced buffer for vertex binding " + std::to_string(binding.binding) + " lacks Vertex usage");
    }

    std::unordered_map<std::uint32_t, const RenderMeshVertexData*> vertexData;
    for (const auto& data : desc.vertexData) {
        if (!vertexData.emplace(data.binding, &data).second)
            throw std::invalid_argument("Mesh contains duplicate CPU vertex data binding " + std::to_string(data.binding));
        if (vertexBuffers.contains(data.binding))
            throw std::invalid_argument("Mesh binding cannot use both an external buffer and CPU vertex data");
    }
    for (const auto& [bindingId, layout] : layouts) {
        if (layout->inputRate != VertexInputRate::PerVertex && layout->inputRate != VertexInputRate::PerInstance)
            throw std::invalid_argument("Mesh has an invalid input rate for binding " + std::to_string(bindingId));
        const std::uint64_t elementCount = layout->inputRate == VertexInputRate::PerInstance ? desc.instanceCount : desc.vertexCount;
        if (elementCount != 0 && layout->stride == 0)
            throw std::invalid_argument("Mesh binding " + std::to_string(bindingId) + " has a nonzero element count and zero stride");
        if (const auto data = vertexData.find(bindingId); data != vertexData.end()) {
            const auto needed = RequiredBytes(0, elementCount, layout->stride, "CPU vertex data");
            if (data->second->bytes.size() < needed)
                throw std::invalid_argument("CPU vertex data for binding " + std::to_string(bindingId) + " is too small for its declared count and stride");
        } else if (const auto buffer = vertexBuffers.find(bindingId); buffer != vertexBuffers.end()) {
            const auto metadata = device->GetBufferDesc(buffer->second->buffer);
            if (!metadata) throw std::invalid_argument("Vertex buffer became invalid during mesh validation");
            const auto needed = RequiredBytes(buffer->second->offset, elementCount, layout->stride, "Vertex buffer");
            if (metadata->size < needed)
                throw std::invalid_argument("Vertex buffer for binding " + std::to_string(bindingId) + " is too small for its declared count and stride");
        } else if (elementCount != 0 && layout->stride != 0) {
            throw std::invalid_argument("Mesh layout binding " + std::to_string(bindingId) + " has no vertex data or buffer");
        }
    }
    for (const auto& [bindingId, _] : vertexBuffers) {
        if (!layouts.contains(bindingId)) throw std::invalid_argument("Vertex buffer binding " + std::to_string(bindingId) + " has no layout description");
    }
    for (const auto& [bindingId, _] : vertexData) {
        if (!layouts.contains(bindingId)) throw std::invalid_argument("CPU vertex data binding " + std::to_string(bindingId) + " has no layout description");
    }

    if (desc.hasIndexData && desc.indexBuffer && desc.indexBuffer->buffer)
        throw std::invalid_argument("Mesh cannot combine CPU index data with an external index buffer");
    if (!desc.hasIndexData && !desc.indexData.empty())
        throw std::invalid_argument("Mesh contains CPU index bytes without marking them as index data");
    if (desc.indexCount && !desc.indexBuffer && !desc.hasIndexData)
        throw std::invalid_argument("Mesh has an index count but no index buffer or CPU index data");
    if (desc.indexBuffer) {
        if (desc.hasIndexData) {
            const auto expected = RequiredBytes(0, desc.indexCount, IndexFormatByteSize(desc.indexBuffer->format), "CPU index data");
            if (desc.indexData.size() != expected)
                throw std::invalid_argument("CPU index data size does not match its index format and declared index count");
        } else {
            if (!desc.indexBuffer->buffer) throw std::invalid_argument("Mesh index buffer handle is invalid");
            if (!device || !device->IsValid(desc.indexBuffer->buffer))
                throw std::invalid_argument("Mesh references an unknown or stale index buffer");
            const auto metadata = device->GetBufferDesc(desc.indexBuffer->buffer);
            if (!metadata) throw std::invalid_argument("Render device has no metadata for the referenced index buffer");
            if (!metadata->usage.Contains(BufferUsage::Index))
                throw std::invalid_argument("Referenced index buffer lacks Index usage");
            const auto needed = RequiredBytes(desc.indexBuffer->offset, desc.indexCount,
                IndexFormatByteSize(desc.indexBuffer->format), "Index buffer");
            if (metadata->size < needed) throw std::invalid_argument("Index buffer is too small for its declared index count and format");
        }
    }
}

} // namespace

RenderMeshManager::RenderMeshManager() {
    static std::atomic<std::uint64_t> nextOwner{1};
    m_ownerId = nextOwner.fetch_add(1, std::memory_order_relaxed);
}

RenderMeshHandle RenderMeshManager::Upload(const RenderMesh& source, IRenderDevice& device) {
    ValidateRenderMeshription(source, &device);
    if (source.vertexData.size() != 1 || source.layout.bindings.size() != 1 ||
        source.layout.bindings.front().inputRate != VertexInputRate::PerVertex ||
        !source.vertexBuffers.empty() || !source.hasIndexData || !source.indexBuffer ||
        !source.vertexCount || !source.indexCount)
        throw std::invalid_argument("Pooled upload requires one interleaved CPU vertex stream and CPU indices");
    const auto& binding = source.layout.bindings.front();
    if (source.vertexData.front().binding != binding.binding)
        throw std::invalid_argument("Pooled vertex data must match the layout binding");
    // Keep each mesh aligned to its stride so offset / stride is a base vertex.
    const auto padding = (binding.stride - m_vertices.size() % binding.stride) % binding.stride;
    const auto vertexOffset = m_vertices.size() + padding;
    const auto firstIndex = m_indices.size();
    if (vertexOffset / binding.stride > std::numeric_limits<std::int32_t>::max() ||
        firstIndex > std::numeric_limits<std::uint32_t>::max() - source.indexCount)
        throw std::length_error("Shared mesh buffers exceed indexed draw limits");
    auto vertices = m_vertices;
    vertices.resize(vertexOffset);
    const auto vertexBytes = static_cast<std::size_t>(source.vertexCount) * binding.stride;
    vertices.insert(vertices.end(), source.vertexData.front().bytes.begin(),
                    source.vertexData.front().bytes.begin() + vertexBytes);
    auto indices = m_indices;
    for (std::uint32_t i = 0; i < source.indexCount; ++i) {
        std::uint32_t index{};
        if (source.indexBuffer->format == IndexFormat::UInt16) {
            std::uint16_t small{};
            std::memcpy(&small, source.indexData.data() + i * sizeof(small), sizeof(small));
            index = small;
        } else {
            std::memcpy(&index, source.indexData.data() + i * sizeof(index), sizeof(index));
        }
        if (index >= source.vertexCount)
            throw std::invalid_argument("Uploaded mesh index is outside its vertex array");
        indices.push_back(index);
    }
    RenderMesh uploaded = source;
    uploaded.vertexData.clear();
    uploaded.indexData.clear();
    uploaded.hasIndexData = false;
    uploaded.vertexBuffers = {{.offset = vertexOffset, .binding = binding.binding}};
    uploaded.indexBuffer = RenderMeshIndexBufferBinding{
        .offset = firstIndex * sizeof(std::uint32_t), .format = IndexFormat::UInt32};
    std::uint32_t slotIndex = 0;
    while (slotIndex < m_slots.size() && m_slots[slotIndex].value) ++slotIndex;
    if (slotIndex == m_slots.size()) m_slots.emplace_back();
    auto entry = std::make_unique<Entry>(Entry{std::move(uploaded), true});
    // Upload transactionally. Existing ranges/handles remain valid if either
    // allocation fails. CPU mirrors allow rebuilding the shared pair on growth.
    auto vertexBuffer = device.CreateBuffer({.size = vertices.size(),
        .usage = BufferUsage::Vertex, .initialData = vertices});
    BufferHandle indexBuffer{};
    try {
        const auto bytes = std::as_bytes(std::span(indices));
        indexBuffer = device.CreateBuffer({.size = bytes.size(),
            .usage = BufferUsage::Index, .initialData = bytes});
        const auto fence = device.CreateFence();
        try {
          device.GetQueue(0)->Submit(QueueSubmitInfo{}, fence);
          device.WaitForFence(fence);
        } catch (...) {
            device.DestroyFence(fence);
            throw;
        }
        device.DestroyFence(fence);
    } catch (...) {
        if (indexBuffer) device.DestroyBuffer(indexBuffer);
        device.DestroyBuffer(vertexBuffer);
        throw;
    }
    const auto oldVertices = m_vertexBuffer, oldIndices = m_indexBuffer;
    m_vertexBuffer = vertexBuffer;
    m_indexBuffer = indexBuffer;
    m_vertices.swap(vertices);
    m_indices.swap(indices);
    m_slots[slotIndex].value = std::move(entry);
    for (auto& slot : m_slots) {
        if (slot.value && slot.value->pooled) {
            slot.value->description.vertexBuffers.front().buffer = m_vertexBuffer;
            slot.value->description.indexBuffer->buffer = m_indexBuffer;
        }
    }
    if (oldVertices) device.DestroyBuffer(oldVertices);
    if (oldIndices) device.DestroyBuffer(oldIndices);
    return {slotIndex, m_slots[slotIndex].generation, m_ownerId};
}

void RenderMeshManager::Release(IRenderDevice& device) {
    if (!m_vertexBuffer && !m_indexBuffer) return;
    device.WaitIdle();
    if (m_vertexBuffer) device.DestroyBuffer(m_vertexBuffer);
    if (m_indexBuffer) device.DestroyBuffer(m_indexBuffer);
    m_vertexBuffer = {};
    m_indexBuffer = {};
    m_slots.clear();
    m_vertices.clear();
    m_indices.clear();
}

RenderMeshHandle RenderMeshManager::Create(const RenderMesh& description, IRenderDevice* device) {
    ValidateRenderMeshription(description, device);
    std::uint32_t index = 0;
    while (index < m_slots.size() && m_slots[index].value) ++index;
    if (index == m_slots.size()) m_slots.emplace_back();
    m_slots[index].value = std::make_unique<Entry>(Entry{description});
    return {index, m_slots[index].generation, m_ownerId};
}

bool RenderMeshManager::Destroy(RenderMeshHandle handle) {
    if (!handle || handle.owner != m_ownerId || handle.index >= m_slots.size()) return false;
    auto& slot = m_slots[handle.index];
    if (!slot.value || slot.generation != handle.generation) return false;
    slot.value.reset();
    if (++slot.generation == 0) ++slot.generation;
    return true;
}

bool RenderMeshManager::IsValid(RenderMeshHandle handle, IRenderDevice* device) const noexcept {
    if (!handle || handle.owner != m_ownerId || handle.index >= m_slots.size()) return false;
    const auto& slot = m_slots[handle.index];
    if (!slot.value || slot.generation != handle.generation) return false;
    const auto& description = slot.value->description;
    const bool referencesBuffers = !description.vertexBuffers.empty() ||
        (description.indexBuffer && static_cast<bool>(description.indexBuffer->buffer));
    if (referencesBuffers && !device) return false;
    const auto validBuffer = [device](BufferHandle buffer) {
        return device && device->IsValid(buffer) && device->GetBufferDesc(buffer).has_value();
    };
    for (const auto& binding : description.vertexBuffers) if (!validBuffer(binding.buffer)) return false;
    if (description.indexBuffer && description.indexBuffer->buffer && !validBuffer(description.indexBuffer->buffer)) return false;
    return true;
}

const RenderMesh* RenderMeshManager::GetDescription(RenderMeshHandle handle, IRenderDevice* device) const noexcept {
    if (!IsValid(handle, device)) return nullptr;
    return &m_slots[handle.index].value->description;
}

RenderMeshUploadStatus RenderMeshManager::GetUploadStatus(RenderMeshHandle handle, IRenderDevice* device) const noexcept {
    const auto* description = GetDescription(handle, device);
    if (!description) return RenderMeshUploadStatus::Invalid;
    return (!description->vertexData.empty() || description->hasIndexData)
        ? RenderMeshUploadStatus::UploadNotImplemented : RenderMeshUploadStatus::Ready;
}

} // namespace Lighthouse
