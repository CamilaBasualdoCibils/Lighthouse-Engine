#include <Lighthouse/Engine/Assets/Assets.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace Lighthouse::Assets {
namespace {

const tinygltf::Accessor& GetAccessor(const tinygltf::Model& model, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= model.accessors.size())
        throw std::runtime_error("glTF mesh references an invalid accessor");
    return model.accessors[static_cast<std::size_t>(index)];
}

struct AccessorBytes {
    const unsigned char* data;
    std::size_t stride;
    std::size_t count;
};

AccessorBytes GetAccessorBytes(const tinygltf::Model& model, const tinygltf::Accessor& accessor,
                               std::size_t elementSize) {
    if (accessor.sparse.isSparse)
        throw std::runtime_error("Sparse glTF mesh accessors are not supported");
    if (accessor.bufferView < 0 ||
        static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size())
        throw std::runtime_error("glTF mesh accessor has no valid buffer view");
    const auto& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || static_cast<std::size_t>(view.buffer) >= model.buffers.size())
        throw std::runtime_error("glTF mesh buffer view references an invalid buffer");
    const auto& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    if (view.byteOffset > buffer.data.size() ||
        view.byteLength > buffer.data.size() - view.byteOffset)
        throw std::runtime_error("glTF mesh buffer view exceeds its buffer");
    const int byteStride = accessor.ByteStride(view);
    if (byteStride <= 0 || static_cast<std::size_t>(byteStride) < elementSize)
        throw std::runtime_error("glTF mesh accessor has an invalid stride");
    const auto stride = static_cast<std::size_t>(byteStride);
    if (accessor.byteOffset > view.byteLength)
        throw std::runtime_error("glTF mesh accessor offset exceeds its buffer view");
    const auto available = view.byteLength - accessor.byteOffset;
    if (accessor.count && (available < elementSize ||
        accessor.count - 1 > (available - elementSize) / stride))
        throw std::runtime_error("glTF mesh accessor exceeds its buffer view");
    return {buffer.data.data() + view.byteOffset + accessor.byteOffset, stride, accessor.count};
}

float ReadFloat(const unsigned char* data) {
    float value;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

AccessorBytes GetFloatAttribute(const tinygltf::Model& model, int index, int type,
                                std::size_t components, std::size_t count) {
    const auto& accessor = GetAccessor(model, index);
    if (accessor.type != type || accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        accessor.count != count)
        throw std::runtime_error("glTF mesh attribute has an unsupported format or vertex count");
    return GetAccessorBytes(model, accessor, components * sizeof(float));
}

std::vector<std::uint32_t> ReadIndices(const tinygltf::Model& model, int index,
                                       std::size_t vertexCount) {
    const auto& accessor = GetAccessor(model, index);
    if (accessor.type != TINYGLTF_TYPE_SCALAR)
        throw std::runtime_error("glTF mesh indices must be scalar");
    std::size_t width;
    switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: width = 1; break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: width = 2; break;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: width = 4; break;
    default: throw std::runtime_error("glTF mesh index component type is unsupported");
    }
    const auto bytes = GetAccessorBytes(model, accessor, width);
    if (bytes.count > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("glTF mesh index count exceeds 32-bit range");
    std::vector<std::uint32_t> indices;
    indices.reserve(bytes.count);
    for (std::size_t i = 0; i < bytes.count; ++i) {
        std::uint32_t value{};
        std::memcpy(&value, bytes.data + i * bytes.stride, width);
        if (value >= vertexCount)
            throw std::runtime_error("glTF mesh index is outside its vertex array");
        indices.push_back(value);
    }
    return indices;
}

void SetPrimitiveTopology(Mesh3D& mesh, int mode) {
    if (mode == -1) mode = TINYGLTF_MODE_TRIANGLES;
    switch (mode) {
    case TINYGLTF_MODE_POINTS: mesh.topology = PrimitiveTopology::PointList; break;
    case TINYGLTF_MODE_LINE: mesh.topology = PrimitiveTopology::LineList; break;
    case TINYGLTF_MODE_LINE_STRIP: mesh.topology = PrimitiveTopology::LineStrip; break;
    case TINYGLTF_MODE_TRIANGLES: mesh.topology = PrimitiveTopology::TriangleList; break;
    case TINYGLTF_MODE_TRIANGLE_STRIP: mesh.topology = PrimitiveTopology::TriangleStrip; break;
    case TINYGLTF_MODE_LINE_LOOP:
    case TINYGLTF_MODE_TRIANGLE_FAN: {
        std::vector<std::uint32_t> source = mesh.indices;
        if (source.empty()) {
            source.resize(mesh.vertices.size());
            std::iota(source.begin(), source.end(), 0u);
        }
        std::vector<std::uint32_t> expanded;
        if (mode == TINYGLTF_MODE_LINE_LOOP) {
            mesh.topology = PrimitiveTopology::LineList;
            if (source.size() < 2) throw std::runtime_error("glTF line loop needs at least two vertices");
            expanded.reserve(source.size() * 2);
            for (std::size_t i = 0; i < source.size(); ++i) {
                expanded.push_back(source[i]);
                expanded.push_back(source[(i + 1) % source.size()]);
            }
        } else {
            mesh.topology = PrimitiveTopology::TriangleList;
            if (source.size() < 3) throw std::runtime_error("glTF triangle fan needs at least three vertices");
            expanded.reserve((source.size() - 2) * 3);
            for (std::size_t i = 1; i + 1 < source.size(); ++i) {
                expanded.push_back(source[0]);
                expanded.push_back(source[i]);
                expanded.push_back(source[i + 1]);
            }
        }
        mesh.indices = std::move(expanded);
        break;
    }
    default: throw std::runtime_error("glTF mesh primitive topology is unsupported");
    }
}

Mesh3D ReadPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive) {
    const auto position = primitive.attributes.find("POSITION");
    if (position == primitive.attributes.end())
        throw std::runtime_error("glTF mesh primitive has no POSITION attribute");
    const auto& positionAccessor = GetAccessor(model, position->second);
    if (positionAccessor.count > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("glTF mesh vertex count exceeds 32-bit range");
    const auto count = positionAccessor.count;
    const auto positions = GetFloatAttribute(model, position->second, TINYGLTF_TYPE_VEC3, 3, count);
    Mesh3D mesh;
    mesh.vertices.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto* data = positions.data + i * positions.stride;
        mesh.vertices[i].position = {ReadFloat(data), ReadFloat(data + 4), ReadFloat(data + 8)};
    }
    if (const auto normal = primitive.attributes.find("NORMAL"); normal != primitive.attributes.end()) {
        const auto normals = GetFloatAttribute(model, normal->second, TINYGLTF_TYPE_VEC3, 3, count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto* data = normals.data + i * normals.stride;
            mesh.vertices[i].normal = {ReadFloat(data), ReadFloat(data + 4), ReadFloat(data + 8)};
        }
    }
    if (const auto uv = primitive.attributes.find("TEXCOORD_0"); uv != primitive.attributes.end()) {
        const auto uvs = GetFloatAttribute(model, uv->second, TINYGLTF_TYPE_VEC2, 2, count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto* data = uvs.data + i * uvs.stride;
            mesh.vertices[i].uv = {ReadFloat(data), ReadFloat(data + 4)};
        }
    }
    if (primitive.indices >= 0)
        mesh.indices = ReadIndices(model, primitive.indices, count);
    SetPrimitiveTopology(mesh, primitive.mode);
    return mesh;
}

} // namespace

GLTFAsset LoadGLTF(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (extension != ".gltf" && extension != ".glb")
        throw std::invalid_argument("LoadGLTF requires a .gltf or .glb file");
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(
        [](tinygltf::Image*, int, std::string*, std::string*, int, int,
           const unsigned char*, int, void*) { return true; }, nullptr);
    tinygltf::Model model;
    std::string error, warning;
    const auto filename = path.string();
    const bool loaded = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error, &warning, filename)
        : loader.LoadASCIIFromFile(&model, &error, &warning, filename);
    if (!loaded)
        throw std::runtime_error("Failed to load glTF '" + filename + "': " + error);
    GLTFAsset result;
    for (const auto& mesh : model.meshes)
        for (const auto& primitive : mesh.primitives)
            result.meshes.push_back(ReadPrimitive(model, primitive));
    return result;
}

} // namespace Lighthouse::Assets
