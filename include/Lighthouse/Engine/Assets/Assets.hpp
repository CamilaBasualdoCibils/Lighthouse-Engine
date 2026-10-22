#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Core/Types.hpp>

namespace Lighthouse {

struct MaterialAsset {};
struct ImageAsset {};
struct TextureAsset {};
struct SkinAsset {};
struct AnimationAsset {};
struct CameraAsset {};

struct SceneNodeAsset {
    std::string name;
    glm::mat4 localTransform{1.0f};
    std::vector<std::uint32_t> children;
    std::optional<std::uint32_t> mesh;
    std::optional<std::uint32_t> camera;
    std::optional<std::uint32_t> skin;
};

struct SceneAsset {
    std::string name;
    std::vector<std::uint32_t> rootNodes;
};

struct GLTFAsset {
    std::vector<Mesh3D> meshes;
    std::vector<MaterialAsset> materials;
    std::vector<ImageAsset> images;
    std::vector<TextureAsset> textures;
    std::vector<SceneNodeAsset> nodes;
    std::vector<SceneAsset> scenes;
    std::vector<SkinAsset> skins;
    std::vector<AnimationAsset> animations;
    std::vector<CameraAsset> cameras;
    std::optional<std::uint32_t> defaultScene;
};

} // namespace Lighthouse

namespace Lighthouse::Assets {

template <typename AssetType>
std::shared_ptr<AssetType> Load(const std::string& path);

GLTFAsset LoadGLTF(const std::filesystem::path& path);

} // namespace Lighthouse::Assets
