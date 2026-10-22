#include <Lighthouse/Engine/Engine.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace {
using namespace Lighthouse;

std::shared_ptr<Mesh> MakeMesh() {
    return std::make_shared<Mesh2D>();
}

const MeshInstance& OnlyMesh(const RenderScene& scene) {
    EXPECT_EQ(scene.meshes.size(), 1u);
    return scene.meshes.begin()->second;
}

TEST(WorldRendererTests, ExtractsMeshAndMaterialWithLocalTransform) {
    Scene scene{"World"};
    auto entity = scene.CreateEntity("Mesh");
    auto mesh = MakeMesh();
    entity.Add<MeshRenderer>(mesh, nullptr);
    Transform transform;
    transform.SetPos(Vec3{2.0f, 3.0f, 4.0f});
    entity.Add<Transform>(transform);

    RenderScene output;
    WorldRenderer{}.Extract(scene, output);

    ASSERT_EQ(output.meshes.size(), 1u);
    const auto& instance = output.meshes.begin()->second;
    EXPECT_EQ(instance.mesh, mesh);
    EXPECT_EQ(instance.material, nullptr);
    EXPECT_EQ(instance.transform, transform.GetMatrix());
}

TEST(WorldRendererTests, ComposesChildTransformWithParentTransform) {
    Scene scene{"Parenting"};
    auto parent = scene.CreateEntity("Parent");
    Transform parentTransform;
    parentTransform.SetPos(Vec3{10.0f, 0.0f, 0.0f});
    parent.Add<Transform>(parentTransform);

    auto child = scene.CreateEntity("Child");
    Transform childTransform;
    childTransform.SetPos(Vec3{0.0f, 2.0f, 0.0f});
    childTransform.SetParent(parent.ID());
    child.Add<MeshRenderer>(MakeMesh(), nullptr);
    child.Add<Transform>(childTransform);

    RenderScene output;
    WorldRenderer{}.Extract(scene, output);

    ASSERT_EQ(output.meshes.size(), 1u);
    const Mat4 expected = parentTransform.GetMatrix() * childTransform.GetMatrix();
    EXPECT_EQ(output.meshes.begin()->second.transform, expected);
}

TEST(WorldRendererTests, ComposesTransformHierarchyFromRootToLeaf) {
    Scene scene{"Hierarchy"};
    auto root = scene.CreateEntity("Root");
    Transform rootTransform;
    rootTransform.SetPos(Vec3{5.0f, 0.0f, 0.0f});
    root.Add<Transform>(rootTransform);

    auto middle = scene.CreateEntity("Middle");
    Transform middleTransform;
    middleTransform.SetPos(Vec3{0.0f, 3.0f, 0.0f});
    middleTransform.SetParent(root.ID());
    middle.Add<Transform>(middleTransform);

    auto leaf = scene.CreateEntity("Leaf");
    Transform leafTransform;
    leafTransform.SetPos(Vec3{0.0f, 0.0f, 7.0f});
    leafTransform.SetParent(middle.ID());
    leaf.Add<MeshRenderer>(MakeMesh(), nullptr);
    leaf.Add<Transform>(leafTransform);

    RenderScene output;
    WorldRenderer{}.Extract(scene, output);

    ASSERT_EQ(output.meshes.size(), 1u);
    const Mat4 expected = rootTransform.GetMatrix() * middleTransform.GetMatrix() * leafTransform.GetMatrix();
    EXPECT_EQ(output.meshes.begin()->second.transform, expected);
    EXPECT_EQ(Vec3{output.meshes.begin()->second.transform[3]}, Vec3(5.0f, 3.0f, 7.0f));
}

TEST(WorldRendererTests, IgnoresEntitiesWithoutMeshesAndSkipsNullMeshes) {
    Scene scene{"Filtering"};
    scene.CreateEntity("No renderer").Add<Transform>();
    auto nullMesh = scene.CreateEntity("Null mesh");
    nullMesh.Add<MeshRenderer>(nullptr, nullptr);
    auto valid = scene.CreateEntity("Valid mesh");
    valid.Add<MeshRenderer>(MakeMesh(), nullptr);

    RenderScene output;
    WorldRenderer{}.Extract(scene, output);

    EXPECT_EQ(output.meshes.size(), 1u);
}

TEST(WorldRendererTests, ClearsPreviousExtractionResults) {
    Scene scene{"Refresh"};
    auto entity = scene.CreateEntity("Mesh");
    entity.Add<MeshRenderer>(MakeMesh(), nullptr);

    RenderScene output;
    output.meshes.emplace(GraphicsPipelineHandle{}, MeshInstance{});
    output.lights.emplace_back();
    output.cameras.emplace_back();
    WorldRenderer{}.Extract(scene, output);

    EXPECT_EQ(output.meshes.size(), 1u);
    EXPECT_TRUE(output.lights.empty());
    EXPECT_TRUE(output.cameras.empty());
}

} // namespace
