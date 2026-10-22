#include "RenderMeshManager.hpp"

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <algorithm>
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
        if (!HasUsage(metadata->usage, BufferUsage::Vertex))
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
            if (!HasUsage(metadata->usage, BufferUsage::Index))
                throw std::invalid_argument("Referenced index buffer lacks Index usage");
            const auto needed = RequiredBytes(desc.indexBuffer->offset, desc.indexCount,
                IndexFormatByteSize(desc.indexBuffer->format), "Index buffer");
            if (metadata->size < needed) throw std::invalid_argument("Index buffer is too small for its declared index count and format");
        }
    }
}

} // namespace

RenderMeshHandle RenderMeshManager::Create(const RenderMesh& description, IRenderDevice* device) {
    ValidateRenderMeshription(description, device);
    std::uint32_t index = 0;
    while (index < m_slots.size() && m_slots[index].value) ++index;
    if (index == m_slots.size()) m_slots.emplace_back();
    m_slots[index].value = std::make_unique<Entry>(Entry{description});
    return {index, m_slots[index].generation};
}

bool RenderMeshManager::Destroy(RenderMeshHandle handle) {
    if (!handle || handle.index >= m_slots.size()) return false;
    auto& slot = m_slots[handle.index];
    if (!slot.value || slot.generation != handle.generation) return false;
    slot.value.reset();
    if (++slot.generation == 0) ++slot.generation;
    return true;
}

bool RenderMeshManager::IsValid(RenderMeshHandle handle, IRenderDevice* device) const noexcept {
    if (!handle || handle.index >= m_slots.size()) return false;
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
