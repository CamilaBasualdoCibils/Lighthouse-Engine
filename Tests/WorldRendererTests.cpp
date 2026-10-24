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

struct CountingSystem final : IWorldRenderSystem {
    int prepared{};
    int drawn{};
    WorldRenderPass lastPass{WorldRenderPass::Forward};
    void Prepare(const RenderScene&) override { ++prepared; }
    void Draw(IRenderCommandList&, WorldRenderPass pass, WorldDrawContext&) override {
        ++drawn;
        lastPass = pass;
    }
};

TEST(WorldRendererTests, RegisteredSystemsCanBePreparedAndDispatched) {
    struct TestRenderer final : WorldRenderer {
        void Run(const RenderScene& scene, IRenderCommandList& commands,
                 WorldDrawContext& context) {
            PrepareSystems(scene);
            DrawSystems(commands, WorldRenderPass::GBuffer, context);
        }
    } renderer;
    auto& system = renderer.AddSystem<CountingSystem>();
    RenderScene scene;
    struct Commands final : IRenderCommandList {
        void BeginRendering(const RenderPassDesc&) override {}
        void EndRendering() override {}
        void BindPipeline(GraphicsPipelineHandle) override {}
        void BindDescriptorSet(std::uint32_t, DescriptorSetHandle) override {}
        void BindVertexBuffer(BufferHandle, std::uint32_t) override {}
        void BindIndexBuffer(BufferHandle) override {}
        void BindTexture(std::uint32_t, ImageHandle) override {}
        void Draw(PrimitiveTopology, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    } commands;
    struct Device final : IRenderDevice {
        SwapchainHandle CreateSwapchain(Window&, const SwapchainConfig&) override { return {}; }
        ImageViewHandle AcquireSwapchainImage(SwapchainHandle) override { return {}; }
        bool DestroySwapchain(SwapchainHandle) override { return false; }
        bool IsValid(SwapchainHandle) const noexcept override { return false; }
        ImageHandle CreateImage(const ImageDesc&) override { return {}; }
        ImageViewHandle CreateImageView(const ImageViewDesc&) override { return {}; }
        BufferHandle CreateBuffer(const BufferDesc&) override { return {}; }
        std::optional<BufferMetadata> GetBufferDesc(BufferHandle) const noexcept override { return {}; }
        bool IsValid(BufferHandle) const noexcept override { return false; }
        bool IsValid(DescriptorSetLayoutHandle) const noexcept override { return false; }
        bool IsValid(DescriptorSetHandle) const noexcept override { return false; }
        bool IsValid(PipelineLayoutHandle) const noexcept override { return false; }
        bool IsValid(SamplerHandle) const noexcept override { return false; }
        ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override { return {}; }
        DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc&) override { return {}; }
        DescriptorSetHandle CreateDescriptorSet(const DescriptorSetDesc&) override { return {}; }
        PipelineLayoutHandle CreatePipelineLayout(const PipelineLayoutDesc&) override { return {}; }
        SamplerHandle CreateSampler(const SamplerDesc&) override { return {}; }
        void UpdateDescriptorSet(DescriptorSetHandle, std::span<const DescriptorWrite>) override {}
        GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override { return {}; }
        std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override { return {}; }
        void Destroy(ImageHandle) override {}
        void Destroy(ImageViewHandle) override {}
        void Destroy(BufferHandle) override {}
        void Destroy(GraphicsPipelineHandle) override {}
        void Destroy(ShaderModuleHandle) override {}
        void Destroy(DescriptorSetLayoutHandle) override {}
        void Destroy(DescriptorSetHandle) override {}
        void Destroy(PipelineLayoutHandle) override {}
        void Destroy(SamplerHandle) override {}
        RenderCapabilities GetCapabilities() const override { return {}; }
        void Submit(const IRenderCommandList&) override {}
        void Present(SwapchainHandle) override {}
    } device;
    WorldDrawContext context{device};
    renderer.Run(scene, commands, context);
    EXPECT_EQ(system.prepared, 1);
    EXPECT_EQ(system.drawn, 1);
    EXPECT_EQ(system.lastPass, WorldRenderPass::GBuffer);
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
