#pragma once
#include <Lighthouse/Engine/Assets/ImageData.hpp>
#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Assets/Shader.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <any>
#include <boost/container/static_vector.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/describe.hpp>
#include <boost/describe/enum.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace Lighthouse
{

struct BufferAsset
{
  struct RawData
  {
    std::vector<std::byte> data;
  };
  struct FileRef
  {
    std::filesystem::path path;
    size_t size;
  };
  std::variant<RawData, FileRef> data;
};
enum class BufferViewTarget : uint16_t
{
  None = 0,
  ArrayBuffer = 34962,
  ElementArrayBuffer = 34963,
};
struct BufferViewAsset
{
  struct View
  {
    uint32_t buffer{};
    uint32_t offset{};
    uint32_t size{};
    BufferViewTarget target{};
    std::optional<std::uint32_t> byteStride;
  };
  boost::container::small_vector<View, 16> views;
};
enum class AccessorComponentType : uint16_t
{
  int8 = 5120,
  uint8 = 5121,
  int16 = 5122,
  uint16 = 5123,
  int32 = 5124,
  uint32 = 5125,
  float32 = 5126,
};
enum class AccessorType : uint8_t
{
  Scalar,
  Vec2,
  Vec3,
  Vec4,
  Mat2,
  Mat3,
  Mat4,
};
struct AccessorBoundsArray
{
  enum class DataType : int8_t
  {
    Int64,
    float64,
  };
  DataType dataType;
  size_t count{};
  union
  {
    boost::container::static_vector<std::int64_t, 16> int64_buffer;
    boost::container::static_vector<double, 16> float64_buffer;
  };

  explicit AccessorBoundsArray(DataType type = DataType::Int64) : dataType(type)
  {
    if (dataType == DataType::Int64)
      std::construct_at(&int64_buffer);
    else
      std::construct_at(&float64_buffer);
  }

  AccessorBoundsArray(const AccessorBoundsArray& other) : AccessorBoundsArray(other.dataType)
  {
    *this = other;
  }

  AccessorBoundsArray(AccessorBoundsArray&& other) : AccessorBoundsArray(other.dataType)
  {
    *this = std::move(other);
  }

  AccessorBoundsArray& operator=(const AccessorBoundsArray& other)
  {
    if (this == &other) return *this;
    SetType(other.dataType);
    if (dataType == DataType::Int64)
      int64_buffer = other.int64_buffer;
    else
      float64_buffer = other.float64_buffer;
    count = other.count;
    return *this;
  }

  AccessorBoundsArray& operator=(AccessorBoundsArray&& other)
  {
    if (this == &other) return *this;
    SetType(other.dataType);
    if (dataType == DataType::Int64)
      int64_buffer = std::move(other.int64_buffer);
    else
      float64_buffer = std::move(other.float64_buffer);
    count = other.count;
    other.count = dataType == DataType::Int64 ? other.int64_buffer.size() : other.float64_buffer.size();
    return *this;
  }

  ~AccessorBoundsArray()
  {
    if (dataType == DataType::Int64)
      std::destroy_at(&int64_buffer);
    else
      std::destroy_at(&float64_buffer);
  }

private:
  void SetType(DataType type)
  {
    if (dataType == type) return;
    if (dataType == DataType::Int64)
    {
      std::destroy_at(&int64_buffer);
      std::construct_at(&float64_buffer);
    }
    else
    {
      std::destroy_at(&float64_buffer);
      std::construct_at(&int64_buffer);
    }
    dataType = type;
  }
};
struct AccessorAsset
{
  std::string name;
  int32_t bufferView{};
  int32_t byteOffset{0};
  AccessorComponentType componentType;
  bool normalized{false};
  size_t count{};
  std::optional<AccessorBoundsArray> min, max;
  AccessorType type;
};
struct PrimitiveAsset
{
  std::optional<std::uint32_t> indices; // Accessor index, not a GPU buffer.
  std::optional<std::uint32_t> material; // Index into GLTFAsset::materials.
  uint32_t position;
  uint32_t normal;
  uint32_t tangent;

  // TEXCOORDS_[n] = m
  // n is the index of the texcoord set (e.g., TEXCOORDS_0, TEXCOORDS_1, etc.)
  // m is an accesor index
  std::unordered_map<uint32_t, uint32_t> texcoords, colors, joints, weights;
};
struct MeshAsset
{
  std::vector<uint32_t> primitives;
};
struct MaterialAsset
{
  struct TextureInfo
  {
    std::uint32_t texture; // Index into GLTFAsset::textures.
    std::uint32_t texCoord{0};
  };
  struct NormalTextureInfo : TextureInfo
  {
    float scale{1.0f};
  };
  struct OcclusionTextureInfo : TextureInfo
  {
    float strength{1.0f};
  };
  enum class AlphaMode : std::uint8_t
  {
    Opaque,
    Mask,
    Blend,
  };

  std::string name;
  glm::vec4 baseColorFactor{1.0f};
  float metallicFactor{1.0f};
  float roughnessFactor{1.0f};
  std::optional<TextureInfo> baseColorTexture;
  std::optional<TextureInfo> metallicRoughnessTexture;
  std::optional<NormalTextureInfo> normalTexture;
  std::optional<OcclusionTextureInfo> occlusionTexture;
  std::optional<TextureInfo> emissiveTexture;
  glm::vec3 emissiveFactor{0.0f};
  AlphaMode alphaMode{AlphaMode::Opaque};
  float alphaCutoff{0.5f};
  bool doubleSided{false};
};
struct TextureAsset
{
  std::uint32_t image{}; // Index into GLTFAsset::images, not a GPU handle.
  std::optional<std::uint32_t> sampler; // Index into GLTFAsset::samplers.
};
struct SamplerAsset
{
  // Numeric glTF filter/wrap constants; no GPU sampler is created.
  std::string name;
  std::optional<std::uint16_t> magFilter;
  std::optional<std::uint16_t> minFilter;
  std::uint16_t wrapS{10497};
  std::uint16_t wrapT{10497};
};
struct SkinAsset
{
};
struct AnimationAsset
{
};
struct CameraAsset
{
  struct Perspective
  {
    std::optional<float> aspectRatio;
    float yfov{}; // Radians, as stored in glTF.
    std::optional<float> zfar; // Omitted means an infinite far plane.
    float znear{};
  };
  struct Orthographic
  {
    float xmag{};
    float ymag{};
    float zfar{};
    float znear{};
  };
  std::string name;
  std::variant<Perspective, Orthographic> projection{Perspective{}};
};

struct SceneNodeAsset
{
  std::string name;
  glm::mat4 localTransform{1.0f};
  std::vector<std::uint32_t> children;
  std::optional<std::uint32_t> mesh;
  std::optional<std::uint32_t> camera;
  std::optional<std::uint32_t> skin;
};
enum class ImageAssetEncoding : uint8_t
{
  Unknown,
  PNG,
  JPEG,
};

struct ImageAsset
{
  ImageAssetEncoding encoding{ImageAssetEncoding::Unknown};
  struct FilePath
  {
    std::filesystem::path path;
  };
  std::variant<std::filesystem::path, std::vector<std::byte>> data;
};
struct SceneAsset
{
  std::string name;
  std::vector<std::uint32_t> rootNodes;
};

struct GLTFAsset
{
  std::vector<BufferAsset> buffers;
  std::vector<BufferViewAsset> bufferViews;
  std::vector<AccessorAsset> accesors;
  std::vector<PrimitiveAsset> primitives;
  std::vector<MeshAsset> meshes;
  std::vector<MaterialAsset> materials;
  std::vector<ImageAsset> images;
  std::vector<TextureAsset> textures;
  std::vector<SamplerAsset> samplers;
  std::vector<SceneNodeAsset> nodes;
  std::vector<SceneAsset> scenes;
  std::vector<SkinAsset> skins;
  std::vector<AnimationAsset> animations;
  std::vector<CameraAsset> cameras;
  std::optional<std::uint32_t> defaultScene;
};

} // namespace Lighthouse
namespace Lighthouse::Assets
{
GLTFAsset ParseGLTF(const std::filesystem::path& path);
std::string LoadText(const std::filesystem::path& path);
struct ShaderLoadOptions
{
  std::optional<ShaderSourceLanguage> language;
  std::optional<ShaderStage> stage;
  std::string entryPoint{"main"};
};

Shader LoadShader(const std::filesystem::path& path,
                  const ShaderLoadOptions& options = {});
Shader LoadShader(std::string_view source, const ShaderCompileOptions& options);
} // namespace Lighthouse::Assets
