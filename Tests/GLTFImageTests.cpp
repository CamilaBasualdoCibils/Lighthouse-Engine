#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/Vulkan/VulkanBackend.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
using namespace Lighthouse;
namespace
{
void Check(bool value,
           std::source_location location = std::source_location::current())
{
  if (!value)
    throw std::runtime_error("glTF image check failed at line " +
                             std::to_string(location.line()));
}
template <class F> void Rejects(F function)
{
  bool rejected = false;
  try
  {
    function();
  }
  catch (const std::exception&)
  {
    rejected = true;
  }
  Check(rejected);
}
struct Fixtures
{
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() /
      ("eesg-gltf-images-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Fixtures();
  ~Fixtures()
  {
    std::error_code error;
    std::filesystem::remove_all(directory, error);
  }
  void Json(const std::string& name, const nlohmann::json& json)
  {
    std::ofstream(directory / name) << json.dump();
  }
};
const std::array<unsigned char, 74> png{
    137, 80,  78,  71, 13, 10, 26,  10, 0,  0,  0,   13,  73,  72, 68,
    82,  0,   0,   0,  2,  0,  0,   0,  1,  8,  6,   0,   0,   0,  244,
    34,  127, 138, 0,  0,  0,  17,  73, 68, 65, 84,  120, 156, 99, 224,
    18,  145, 211, 56, 49, 45, 229, 63, 0,  9,  10,  3,   38,  10, 143,
    2,   229, 0,   0,  0,  0,  73,  69, 78, 68, 174, 66,  96,  130};
const char* pngURI =
    "data:image/"
    "png;base64,iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+"
    "KAAAAEUlEQVR4nGPgEpHTODEt5T8ACQoDJgqPAuUAAAAASUVORK5CYII=";
void Word(std::ofstream& file, uint32_t value)
{
  for (int i = 0; i < 4; ++i)
    file.put(char((value >> (8 * i)) & 255));
}
Fixtures::Fixtures()
{
  std::filesystem::create_directory(directory);
  {
    std::ofstream file(directory / "pixels.png", std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), png.size());
  }
  const auto shared = nlohmann::json::parse(R"({
      "asset": {"version": "2.0"},
      "images": [{"uri": "pixels.png"}],
      "textures": [{"source": 0}, {"source": 0}]
  })");
  Json("External.gltf", shared);
  auto embedded = shared;
  embedded["images"][0]["uri"] = pngURI;
  Json("DataURI.gltf", embedded);
  auto binary = shared;
  binary["images"][0] = {{"bufferView", 0}, {"mimeType", "image/png"}};
  binary["buffers"] = {{{"byteLength", png.size()}}};
  binary["bufferViews"] = {
      {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", png.size()}}};
  auto json = binary.dump();
  while (json.size() % 4)
    json += ' ';
  const uint32_t binSize = uint32_t((png.size() + 3) & ~size_t(3));
  {
    std::ofstream file(directory / "Embedded.glb", std::ios::binary);
    Word(file, 0x46546c67);
    Word(file, 2);
    Word(file, 12 + 8 + uint32_t(json.size()) + 8 + binSize);
    Word(file, uint32_t(json.size()));
    Word(file, 0x4e4f534a);
    file << json;
    Word(file, binSize);
    Word(file, 0x004e4942);
    file.write(reinterpret_cast<const char*>(png.data()), png.size());
    for (size_t i = png.size(); i < binSize; ++i)
      file.put(0);
  }
  auto invalid = shared;
  invalid["textures"][0]["source"] = 99;
  Json("BadSource.gltf", invalid);
  invalid = shared;
  invalid["images"][0]["uri"] = "Missing.png";
  Json("Missing.gltf", invalid);
  {
    std::ofstream(directory / "Broken.png") << "not a PNG";
  }
  invalid["images"][0]["uri"] = "Broken.png";
  Json("Broken.gltf", invalid);
  auto twoImages = shared;
  twoImages["images"].push_back({{"uri", "pixels.png"}});
  twoImages["textures"][1]["source"] = 1;
  Json("TwoImages.gltf", twoImages);
}
void TestUploads(IRenderDevice& device, const Fixtures& fixtures)
{
  for (auto file : {"External.gltf", "DataURI.gltf", "Embedded.glb"})
  {
    const auto asset = Assets::LoadGLTF(fixtures.directory / file);
    Check(asset.images.size() == 1 && asset.textures.size() == 2);
    Check(asset.textures[0].image == 0 && asset.textures[1].image == 0);
    const auto& data = asset.images[0];
    Check(data.GetResolution() == UVec2(2, 1));
    Check(data.GetFormat() == ImageFormat::RGBA8);
    constexpr std::array<unsigned, 8> expected{10,  20,  30,  40,
                                               200, 150, 100, 255};
    Check(data.GetData().size() == expected.size());
    auto copy = data;
    Check(copy.GetData().data() != data.GetData().data());
    const auto image = device.CreateImage(
        data.ToImageDesc({ImageUsage::Sampled, ImageUsage::TransferSource}));
    const auto pixels = device.ReadImage(image);
    Check(pixels.size() == expected.size());
    for (size_t i = 0; i < pixels.size(); ++i)
    {
      Check(std::to_integer<unsigned>(pixels[i]) == expected[i]);
      Check(pixels[i] == data.GetData()[i]);
    }
    device.DestroyImage(image);
  }
  for (auto file : {"BadSource.gltf", "Missing.gltf", "Broken.gltf"})
    Rejects([&] { Assets::LoadGLTF(fixtures.directory / file); });
  Check(Assets::LoadGLTF(fixtures.directory / "TwoImages.gltf").images.size() ==
        2);
  Rejects([] { ImageData({0, 1}, ImageFormat::RGBA8, {}); });
  Rejects(
      [] { ImageData({1, 1}, ImageFormat::RGBA8, std::vector<std::byte>(3)); });
  Rejects([] { ImageData({1, 1}, ImageFormat::Undefined, {}); });
  const auto meshPath =
      std::filesystem::path(LIGHTHOUSE_TEST_SOURCE_DIR) /
      "Tests/Fixtures/SimpleMeshes.gltf";
  const auto meshes = Assets::LoadGLTF(meshPath);
  Check(meshes.meshes.size() == 2 && meshes.images.empty() &&
        meshes.textures.empty());
}

} // namespace
int main() {
  Fixtures fixtures;
  for (bool vulkan : {false, true})
  {
    std::unique_ptr<IGraphicsBackend> backend;
    if (vulkan)
      backend = std::make_unique<VulkanBackend>();
    else
      backend = std::make_unique<OpenGLBackend>();
    std::string diagnostic;
    if (!backend->Initialize(diagnostic))
      throw std::runtime_error(diagnostic);
    {
      auto device = backend->CreateDevice(
          {.applicationName = "glTF GPU image tests",
           .queues = {
               {.capabilities = Lighthouse::RenderQueueCapability::Graphics |
                                Lighthouse::RenderQueueCapability::Compute |
                                Lighthouse::RenderQueueCapability::Transfer,
                .minCount = 1}}});
      std::cout << device->GetCapabilities().deviceName << '\n';
      TestUploads(*device, fixtures);

    }
    backend->Shutdown();
  }
}
