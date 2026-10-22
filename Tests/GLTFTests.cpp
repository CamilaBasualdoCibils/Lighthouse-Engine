#include <Lighthouse/Engine/Assets/Assets.hpp>

#include <filesystem>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {
using namespace Lighthouse;

std::filesystem::path Fixture(const char* name) {
    return std::filesystem::path(__FILE__).parent_path() / "Fixtures" / name;
}

void CheckMeshes(const GLTFAsset& asset) {
    ASSERT_EQ(asset.meshes.size(), 2u);
    const auto& triangle = asset.meshes[0];
    ASSERT_EQ(triangle.vertices.size(), 3u);
    EXPECT_EQ(triangle.indices, (std::vector<std::uint32_t>{0, 1, 2}));
    EXPECT_EQ(triangle.topology, PrimitiveTopology::TriangleList);
    EXPECT_FLOAT_EQ(triangle.vertices[1].position.x, 1.0f);
    EXPECT_FLOAT_EQ(triangle.vertices[1].normal.z, 1.0f);
    EXPECT_FLOAT_EQ(triangle.vertices[1].uv.x, 1.0f);
    EXPECT_EQ(triangle.ToRenderMesh().indexCount, 3u);

    const auto& points = asset.meshes[1];
    EXPECT_EQ(points.topology, PrimitiveTopology::PointList);
    EXPECT_TRUE(points.indices.empty());
    ASSERT_EQ(points.vertices.size(), 3u);
    EXPECT_FLOAT_EQ(points.vertices[2].position.y, 1.0f);
    EXPECT_FLOAT_EQ(points.vertices[2].normal.z, 0.0f);
    EXPECT_EQ(points.ToRenderMesh().vertexCount, 3u);

    EXPECT_TRUE(asset.materials.empty());
    EXPECT_TRUE(asset.images.empty());
    EXPECT_TRUE(asset.textures.empty());
    EXPECT_TRUE(asset.nodes.empty());
    EXPECT_TRUE(asset.scenes.empty());
    EXPECT_TRUE(asset.skins.empty());
    EXPECT_TRUE(asset.animations.empty());
    EXPECT_TRUE(asset.cameras.empty());
    EXPECT_FALSE(asset.defaultScene.has_value());
}

TEST(GLTFTests, LoadsAsciiGLTFMeshes) {
    CheckMeshes(Assets::LoadGLTF(Fixture("SimpleMeshes.gltf")));
}

TEST(GLTFTests, LoadsBinaryGLBMeshes) {
    CheckMeshes(Assets::LoadGLTF(Fixture("SimpleMeshes.glb")));
}

TEST(GLTFTests, RejectsMissingAndUnsupportedFiles) {
    EXPECT_THROW(Assets::LoadGLTF(Fixture("Missing.gltf")), std::runtime_error);
    EXPECT_THROW(Assets::LoadGLTF(Fixture("SimpleMeshes.obj")), std::invalid_argument);
}

} // namespace
