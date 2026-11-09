#include <Lighthouse/Engine/Core/Profiling.hpp>
#include <Lighthouse/Engine/Assets/Assets.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <charconv>
#include <memory>
#include <type_traits>

#include <algorithm>
#include <cctype>
#include <fastgltf/types.hpp>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace Lighthouse::Assets {
namespace {

GLTFAsset LoadGLTFImpl(const std::filesystem::path& path) {
    LIGHTHOUSE_PROFILE_SCOPE("Assets::LoadGLTF");
    fastgltf::Parser parser;
    auto data = fastgltf::GltfDataBuffer::FromPath(path);
    if (data.error() != fastgltf::Error::None)
        throw std::runtime_error("Failed to read glTF file: " + path.string());

    auto asset = parser.loadGltf(data.get(), path.parent_path(), fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None)
        throw std::runtime_error("Failed to parse glTF file: " + path.string() + ": " +
                                 std::string(fastgltf::getErrorMessage(asset.error())));

    GLTFAsset gltfAsset;
    const auto index = [](std::size_t value) -> std::uint32_t {
        if (value > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("glTF index or byte range exceeds 32 bits");
        return static_cast<std::uint32_t>(value);
    };
    const auto filePath = [&](const fastgltf::sources::URI& source) {
        if (!source.uri.isLocalPath() || source.fileByteOffset != 0)
            throw std::runtime_error("glTF URI cannot be represented as a local file reference");
        return (path.parent_path() / source.uri.fspath()).lexically_normal();
    };
    gltfAsset.buffers.reserve(asset->buffers.size());
    for (const auto& buffer : asset->buffers) {
        BufferAsset bufferAsset;
        std::visit([&](const auto& source) {
            using T = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<T, fastgltf::sources::URI>)
                bufferAsset.data = BufferAsset::FileRef{filePath(source), buffer.byteLength};
            else if constexpr (std::is_same_v<T, fastgltf::sources::Array> ||
                               std::is_same_v<T, fastgltf::sources::Vector> ||
                               std::is_same_v<T, fastgltf::sources::ByteView>) {
                if (source.bytes.size() < buffer.byteLength)
                    throw std::runtime_error("glTF buffer is shorter than its declared byteLength");
                bufferAsset.data = BufferAsset::RawData{
                    std::vector<std::byte>(source.bytes.begin(), source.bytes.end())};
            } else
                throw std::runtime_error("Unsupported glTF buffer source");
        }, buffer.data);
        gltfAsset.buffers.push_back(std::move(bufferAsset));
    }

    for (const auto& view : asset->bufferViews) {
        if (view.meshoptCompression)
            throw std::runtime_error("BufferViewAsset cannot represent compressed buffer views");
        BufferViewAsset result;
        result.views.push_back({index(view.bufferIndex), index(view.byteOffset),
                                index(view.byteLength), view.target
                                    ? static_cast<BufferViewTarget>(*view.target)
                                    : BufferViewTarget::None,
                                view.byteStride ? std::optional{index(*view.byteStride)} : std::nullopt});
        gltfAsset.bufferViews.push_back(std::move(result));
    }

    for (const auto& accessor : asset->accessors) {
        if (accessor.sparse)
            throw std::runtime_error("AccessorAsset cannot represent sparse accessors");
        AccessorAsset result{};
        result.name = accessor.name;
        if ((accessor.bufferViewIndex && *accessor.bufferViewIndex > INT32_MAX) ||
            accessor.byteOffset > INT32_MAX)
            throw std::runtime_error("glTF accessor exceeds the signed 32-bit range");
        result.bufferView = accessor.bufferViewIndex
            ? static_cast<std::int32_t>(*accessor.bufferViewIndex) : -1;
        result.byteOffset = static_cast<std::int32_t>(accessor.byteOffset);
        result.componentType = static_cast<AccessorComponentType>(
            fastgltf::getGLComponentType(accessor.componentType));
        result.normalized = accessor.normalized;
        result.count = accessor.count;
        switch (accessor.type) {
        case fastgltf::AccessorType::Scalar: result.type = AccessorType::Scalar; break;
        case fastgltf::AccessorType::Vec2: result.type = AccessorType::Vec2; break;
        case fastgltf::AccessorType::Vec3: result.type = AccessorType::Vec3; break;
        case fastgltf::AccessorType::Vec4: result.type = AccessorType::Vec4; break;
        case fastgltf::AccessorType::Mat2: result.type = AccessorType::Mat2; break;
        case fastgltf::AccessorType::Mat3: result.type = AccessorType::Mat3; break;
        case fastgltf::AccessorType::Mat4: result.type = AccessorType::Mat4; break;
        default: throw std::runtime_error("Invalid glTF accessor type");
        }
        const auto bounds = [](const auto& source, auto& destination) {
            if (!source) return;
            destination.emplace(source->template isType<double>()
                ? AccessorBoundsArray::DataType::float64 : AccessorBoundsArray::DataType::Int64);
            auto& target = *destination;
            target.count = source->size();
            if (source->template isType<double>()) {
                for (std::size_t i = 0; i < target.count; ++i)
                    target.float64_buffer.push_back(source->template get<double>(i));
            } else {
                for (std::size_t i = 0; i < target.count; ++i)
                    target.int64_buffer.push_back(source->template get<std::int64_t>(i));
            }
        };
        bounds(accessor.min, result.min);
        bounds(accessor.max, result.max);
        gltfAsset.accesors.push_back(std::move(result));
    }

    for (const auto& mesh : asset->meshes) {
        MeshAsset result;
        for (const auto& primitive : mesh.primitives) {
            // These fields have no storage in PrimitiveAsset yet.
            if (!primitive.targets.empty() ||
                primitive.type != fastgltf::PrimitiveType::Triangles)
                throw std::runtime_error("PrimitiveAsset cannot represent morph targets or non-triangle topology");
            PrimitiveAsset output{};
            if (primitive.indicesAccessor) output.indices = index(*primitive.indicesAccessor);
            if (primitive.materialIndex) output.material = index(*primitive.materialIndex);
            output.position = output.normal = output.tangent = UINT32_MAX;
            for (const auto& attribute : primitive.attributes) {
                const std::string_view name = attribute.name;
                const auto value = index(attribute.accessorIndex);
                if (name == "POSITION") output.position = value;
                else if (name == "NORMAL") output.normal = value;
                else if (name == "TANGENT") output.tangent = value;
                else {
                    const auto set = [&](std::string_view prefix, auto& map) {
                        if (!name.starts_with(prefix)) return false;
                        std::uint32_t number{};
                        const auto suffix = name.substr(prefix.size());
                        const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), number);
                        if (parsed.ec != std::errc{} || parsed.ptr != suffix.data() + suffix.size())
                            throw std::runtime_error("Invalid glTF attribute set index");
                        map.emplace(number, value);
                        return true;
                    };
                    if (!set("TEXCOORD_", output.texcoords) && !set("COLOR_", output.colors) &&
                        !set("JOINTS_", output.joints) && !set("WEIGHTS_", output.weights))
                        throw std::runtime_error("PrimitiveAsset cannot represent attribute: " + std::string(name));
                }
            }
            result.primitives.push_back(index(gltfAsset.primitives.size()));
            gltfAsset.primitives.push_back(std::move(output));
        }
        gltfAsset.meshes.push_back(std::move(result));
    }

    const auto textureInfo = [&](const fastgltf::TextureInfo& source) {
        return MaterialAsset::TextureInfo{index(source.textureIndex), index(source.texCoordIndex)};
    };
    gltfAsset.materials.reserve(asset->materials.size());
    for (const auto& material : asset->materials) {
        MaterialAsset result;
        result.name = material.name;
        for (std::size_t i = 0; i < 4; ++i)
            result.baseColorFactor[i] = material.pbrData.baseColorFactor[i];
        result.metallicFactor = material.pbrData.metallicFactor;
        result.roughnessFactor = material.pbrData.roughnessFactor;
        if (material.pbrData.baseColorTexture)
            result.baseColorTexture = textureInfo(*material.pbrData.baseColorTexture);
        if (material.pbrData.metallicRoughnessTexture)
            result.metallicRoughnessTexture = textureInfo(*material.pbrData.metallicRoughnessTexture);
        if (material.normalTexture)
            result.normalTexture = MaterialAsset::NormalTextureInfo{
                textureInfo(*material.normalTexture), material.normalTexture->scale};
        if (material.occlusionTexture)
            result.occlusionTexture = MaterialAsset::OcclusionTextureInfo{
                textureInfo(*material.occlusionTexture), material.occlusionTexture->strength};
        if (material.emissiveTexture)
            result.emissiveTexture = textureInfo(*material.emissiveTexture);
        for (std::size_t i = 0; i < 3; ++i)
            result.emissiveFactor[i] = material.emissiveFactor[i];
        switch (material.alphaMode) {
        case fastgltf::AlphaMode::Opaque: result.alphaMode = MaterialAsset::AlphaMode::Opaque; break;
        case fastgltf::AlphaMode::Mask: result.alphaMode = MaterialAsset::AlphaMode::Mask; break;
        case fastgltf::AlphaMode::Blend: result.alphaMode = MaterialAsset::AlphaMode::Blend; break;
        }
        result.alphaCutoff = material.alphaCutoff;
        result.doubleSided = material.doubleSided;
        gltfAsset.materials.push_back(std::move(result));
    }

    gltfAsset.cameras.reserve(asset->cameras.size());
    for (const auto& camera : asset->cameras) {
        CameraAsset result;
        result.name = camera.name;
        std::visit([&](const auto& source) {
            using T = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<T, fastgltf::Camera::Perspective>) {
                CameraAsset::Perspective projection;
                if (source.aspectRatio) projection.aspectRatio = *source.aspectRatio;
                projection.yfov = source.yfov;
                if (source.zfar) projection.zfar = *source.zfar;
                projection.znear = source.znear;
                result.projection = projection;
            } else {
                result.projection = CameraAsset::Orthographic{
                    source.xmag, source.ymag, source.zfar, source.znear};
            }
        }, camera.camera);
        gltfAsset.cameras.push_back(std::move(result));
    }

    for (const auto& image : asset->images) {
        ImageAsset result;
        std::visit([&](const auto& source) {
            using T = std::decay_t<decltype(source)>;
            if constexpr (requires { source.mimeType; }) {
                if (source.mimeType == fastgltf::MimeType::PNG) result.encoding = ImageAssetEncoding::PNG;
                if (source.mimeType == fastgltf::MimeType::JPEG) result.encoding = ImageAssetEncoding::JPEG;
            }
            if constexpr (std::is_same_v<T, fastgltf::sources::URI>) {
                result.data = filePath(source);
                auto extension = std::get<std::filesystem::path>(result.data).extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (result.encoding == ImageAssetEncoding::Unknown) {
                    if (extension == ".png") result.encoding = ImageAssetEncoding::PNG;
                    if (extension == ".jpg" || extension == ".jpeg") result.encoding = ImageAssetEncoding::JPEG;
                }
            } else if constexpr (std::is_same_v<T, fastgltf::sources::Array> ||
                                 std::is_same_v<T, fastgltf::sources::Vector> ||
                                 std::is_same_v<T, fastgltf::sources::ByteView>)
                result.data = std::vector<std::byte>(source.bytes.begin(), source.bytes.end());
            else if constexpr (std::is_same_v<T, fastgltf::sources::BufferView>) {
                const auto& view = asset->bufferViews.at(source.bufferViewIndex);
                const auto& buffer = gltfAsset.buffers.at(view.bufferIndex);
                std::vector<std::byte> bytes(view.byteLength);
                if (const auto* raw = std::get_if<BufferAsset::RawData>(&buffer.data)) {
                    if (view.byteOffset > raw->data.size() || view.byteLength > raw->data.size() - view.byteOffset)
                        throw std::runtime_error("glTF image buffer view is out of bounds");
                    std::copy_n(raw->data.begin() + view.byteOffset, view.byteLength, bytes.begin());
                } else {
                    const auto& file = std::get<BufferAsset::FileRef>(buffer.data);
                    if (view.byteOffset > file.size || view.byteLength > file.size - view.byteOffset)
                        throw std::runtime_error("glTF image buffer view is out of bounds");
                    std::ifstream input(file.path, std::ios::binary);
                    input.seekg(view.byteOffset);
                    if (!input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()))
                        throw std::runtime_error("Failed to read embedded glTF image: " + file.path.string());
                }
                result.data = std::move(bytes);
            } else
                throw std::runtime_error("Unsupported glTF image source");
        }, image.data);
        gltfAsset.images.push_back(std::move(result));
    }
    for (const auto& sampler : asset->samplers) {
        SamplerAsset result;
        result.name = sampler.name;
        if (sampler.magFilter) result.magFilter = static_cast<std::uint16_t>(*sampler.magFilter);
        if (sampler.minFilter) result.minFilter = static_cast<std::uint16_t>(*sampler.minFilter);
        result.wrapS = static_cast<std::uint16_t>(sampler.wrapS);
        result.wrapT = static_cast<std::uint16_t>(sampler.wrapT);
        gltfAsset.samplers.push_back(std::move(result));
    }
    for (const auto& texture : asset->textures) {
        if (!texture.imageIndex)
            throw std::runtime_error("TextureAsset requires an image");
        TextureAsset result{index(*texture.imageIndex)};
        if (texture.samplerIndex) result.sampler = index(*texture.samplerIndex);
        gltfAsset.textures.push_back(std::move(result));
    }
    for (const auto& node : asset->nodes) {
        SceneNodeAsset result;
        result.name = node.name;
        const auto transform = fastgltf::getTransformMatrix(node);
        for (std::size_t column = 0; column < 4; ++column)
            for (std::size_t row = 0; row < 4; ++row)
                result.localTransform[column][row] = transform[column][row];
        for (const auto child : node.children) result.children.push_back(index(child));
        if (node.meshIndex) result.mesh = index(*node.meshIndex);
        if (node.cameraIndex) result.camera = index(*node.cameraIndex);
        if (node.skinIndex) result.skin = index(*node.skinIndex);
        gltfAsset.nodes.push_back(std::move(result));
    }
    for (const auto& scene : asset->scenes) {
        SceneAsset result;
        result.name = scene.name;
        for (const auto node : scene.nodeIndices) result.rootNodes.push_back(index(node));
        gltfAsset.scenes.push_back(std::move(result));
    }
    if (asset->defaultScene) gltfAsset.defaultScene = index(*asset->defaultScene);
    if (!asset->skins.empty() || !asset->animations.empty())
        throw std::runtime_error("SkinAsset and AnimationAsset are still empty definitions");
    return gltfAsset;
}

} // namespace

GLTFAsset ParseGLTF(const std::filesystem::path& path) {
    return LoadGLTFImpl(path);
}

} // namespace Lighthouse::Assets
