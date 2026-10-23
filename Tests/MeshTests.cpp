#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Assets/Mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>
#include <unordered_map>

#include <gtest/gtest.h>

namespace {
using namespace Lighthouse;

class TestDevice final : public IRenderDevice {
public:
    SwapchainHandle CreateSwapchain(Window&, const SwapchainConfig&) override { return {0, 1, DeviceId()}; }
    ImageViewHandle AcquireSwapchainImage(SwapchainHandle) override { return {}; }
    bool DestroySwapchain(SwapchainHandle) override { return true; }
    bool IsValid(SwapchainHandle handle) const noexcept override { return handle.owner == DeviceId() && handle.index == 0 && handle.generation == 1; }
    ImageHandle CreateImage(const ImageDesc&) override { return {}; }
    ImageViewHandle CreateImageView(const ImageViewDesc&) override { return {}; }
    BufferHandle CreateBuffer(const BufferDesc& desc) override {
        BufferHandle handle{m_next++, 1, DeviceId()};
        m_buffers.emplace(handle, BufferMetadata{desc.size, desc.usage, desc.memory});
        return handle;
    }
    std::optional<BufferMetadata> GetBufferDesc(BufferHandle handle) const noexcept override {
        const auto it = m_buffers.find(handle);
        return it == m_buffers.end() ? std::nullopt : std::optional<BufferMetadata>{it->second};
    }
    bool IsValid(BufferHandle handle) const noexcept override { return m_buffers.contains(handle); }
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
    void Destroy(BufferHandle handle) override { m_buffers.erase(handle); }
    void Destroy(GraphicsPipelineHandle) override {}
    void Destroy(ShaderModuleHandle) override {}
    void Destroy(DescriptorSetLayoutHandle) override {}
    void Destroy(DescriptorSetHandle) override {}
    void Destroy(PipelineLayoutHandle) override {}
    void Destroy(SamplerHandle) override {}
    RenderCapabilities GetCapabilities() const override { return {}; }
    void Submit(const IRenderCommandList&) override {}
    void Present(SwapchainHandle) override {}

private:
    std::uint32_t m_next{1};
    struct HandleHash { std::size_t operator()(BufferHandle handle) const noexcept { return handle.index; } };
    std::unordered_map<BufferHandle, BufferMetadata, HandleHash> m_buffers;
};

BufferHandle MakeBuffer(TestDevice& device, std::uint64_t bytes, BufferUsage usage) {
    return device.CreateBuffer(BufferDesc{.size = bytes, .usage = usage});
}

TEST(MeshTests, PositionOnlyMeshDoesNotRequireOtherSemantics) {
    const auto desc = RenderMeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexCount(3).Build();
    EXPECT_EQ(desc.layout.attributes.size(), 1u);
    EXPECT_EQ(desc.layout.bindings.front().stride, 12u);
}

TEST(MeshTests, Mesh2DProducesOwnedRenderMeshData) {
    Mesh2D asset;
    asset.vertices = {{{0.0f, 0.0f}, {0.0f, 0.0f}},
                      {{1.0f, 0.0f}, {1.0f, 0.0f}},
                      {{0.0f, 1.0f}, {0.0f, 1.0f}}};
    asset.indices = {0, 1, 2};
    const Mesh& base = asset;
    const auto renderMesh = base.ToRenderMesh();
    ASSERT_EQ(renderMesh.layout.attributes.size(), 2u);
    EXPECT_EQ(renderMesh.layout.attributes[0].format, VertexFormat::Float2);
    EXPECT_EQ(renderMesh.layout.attributes[1].semantic, "TEXCOORD0");
    EXPECT_EQ(renderMesh.layout.bindings[0].stride, sizeof(Mesh2DVertex));
    EXPECT_EQ(renderMesh.vertexCount, 3u);
    EXPECT_EQ(renderMesh.indexCount, 3u);
    ASSERT_EQ(renderMesh.vertexData.size(), 1u);
    EXPECT_EQ(renderMesh.vertexData[0].bytes.size(), sizeof(Mesh2DVertex) * 3);
    EXPECT_EQ(renderMesh.indexData.size(), sizeof(std::uint32_t) * 3);
    asset.vertices.clear();
    asset.indices.clear();
    EXPECT_EQ(renderMesh.vertexData[0].bytes.size(), sizeof(Mesh2DVertex) * 3);
    Renderer renderer;
    const auto handle = renderer.CreateRenderMesh(renderMesh);
    EXPECT_TRUE(renderer.IsValid(handle));
    EXPECT_EQ(renderer.GetRenderMeshUploadStatus(handle), RenderMeshUploadStatus::UploadNotImplemented);
}

TEST(MeshTests, Mesh3DProducesPositionNormalAndUvLayout) {
    Mesh3D asset;
    asset.vertices = {{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}}};
    asset.topology = PrimitiveTopology::PointList;
    const auto renderMesh = asset.ToRenderMesh();
    ASSERT_EQ(renderMesh.layout.attributes.size(), 3u);
    EXPECT_EQ(renderMesh.layout.attributes[0].format, VertexFormat::Float3);
    EXPECT_EQ(renderMesh.layout.attributes[1].semantic, "NORMAL");
    EXPECT_EQ(renderMesh.layout.attributes[2].format, VertexFormat::Float2);
    EXPECT_EQ(renderMesh.layout.bindings[0].stride, sizeof(Mesh3DVertex));
    EXPECT_EQ(renderMesh.topology, PrimitiveTopology::PointList);
    EXPECT_FALSE(renderMesh.indexBuffer.has_value());
    asset.indices = {1};
    EXPECT_THROW(asset.ToRenderMesh(), std::invalid_argument);
}

TEST(MeshTests, SupportsArbitraryCustomSemanticsAndIntegerFormats) {
    const auto desc = RenderMeshBuilder().Attribute("CUSTOM_DATA", VertexFormat::UInt4, 0)
        .Attribute("JOINTS0", VertexFormat::Int4, 1).Build();
    EXPECT_EQ(desc.layout.attributes[0].semantic, "CUSTOM_DATA");
    EXPECT_EQ(desc.layout.attributes[1].semantic, "JOINTS0");
    EXPECT_EQ(VertexFormatByteSize(VertexFormat::SNorm8x4), 4u);
}

TEST(MeshTests, BuildsInterleavedOffsetsAndAutomaticStride) {
    struct Vertex { float position[3]; float normal[3]; float uv[2]; };
    const auto desc = RenderMeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .Attribute("NORMAL", VertexFormat::Float3, 1)
        .Attribute("TEXCOORD0", VertexFormat::Float2, 2).Build();
    ASSERT_EQ(desc.layout.attributes.size(), 3u);
    EXPECT_EQ(desc.layout.attributes[0].offset, 0u);
    EXPECT_EQ(desc.layout.attributes[1].offset, 12u);
    EXPECT_EQ(desc.layout.attributes[2].offset, 24u);
    EXPECT_EQ(desc.layout.bindings[0].stride, sizeof(Vertex));
}

TEST(MeshTests, SupportsMultipleIndependentVertexBindings) {
    auto desc = RenderMeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0, 0)
        .Attribute("NORMAL", VertexFormat::Float3, 1, 1)
        .VertexBuffer({0, 1}, 0).VertexBuffer({1, 1}, 1).VertexCount(8).Build();
    ASSERT_EQ(desc.layout.bindings.size(), 2u);
    EXPECT_EQ(desc.vertexBuffers[1].binding, 1u);
}

TEST(MeshTests, ExplicitOffsetsAndStridesArePreservedAndValidated) {
    const auto desc = RenderMeshBuilder().Attribute("A", VertexFormat::Float3, 3, 0, 8)
        .VertexStride(24).Build();
    EXPECT_EQ(desc.layout.attributes[0].offset, 8u);
    EXPECT_EQ(desc.layout.bindings[0].stride, 24u);
    EXPECT_THROW(RenderMeshBuilder().Attribute("A", VertexFormat::Float4, 0, 0, 8).VertexStride(20).Build(), std::invalid_argument);
}

TEST(MeshTests, HandlesIndexedAndNonIndexedMeshes) {
    const auto nonIndexed = RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0).VertexCount(3).Build();
    EXPECT_FALSE(nonIndexed.indexBuffer.has_value());
    const auto indexed = RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexCount(3).IndexBuffer({5, 1}).IndexCount(3)
        .VertexBuffer({4, 1}).Build();
    EXPECT_EQ(indexed.indexBuffer->format, IndexFormat::UInt32);
    EXPECT_EQ(indexed.topology, PrimitiveTopology::TriangleList);
}

TEST(MeshTests, ValidatesPerInstanceDataAgainstInstanceCount) {
    std::array<float, 12> instances{};
    auto builder = RenderMeshBuilder().Attribute("TRANSFORM", VertexFormat::Float3, 0)
        .InputRate(VertexInputRate::PerInstance).VertexData(std::span<const float>(instances))
        .InstanceCount(4).VertexCount(100);
    const auto desc = builder.Build();
    EXPECT_EQ(desc.vertexData[0].bytes.size(), sizeof(instances));
    EXPECT_EQ(desc.layout.bindings[0].inputRate, VertexInputRate::PerInstance);
}

TEST(MeshTests, DefaultsCpuDataMemoryToDeviceLocalGpuRead) {
    std::array<float, 3> positions{};
    const auto desc = RenderMeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexData(AsBytes(positions)).VertexCount(1).Build();
    EXPECT_EQ(desc.vertexData[0].memory.preferredLocation, MemoryLocation::DeviceLocal);
    EXPECT_EQ(desc.vertexData[0].memory.requiredAccess, MemoryAccess::GPURead);
    EXPECT_EQ(desc.vertexData[0].memory.preferredAccess, MemoryAccess::None);
}

TEST(MeshTests, SupportsHostVisibleCpuDataMemoryPreferences) {
    std::array<float, 3> positions{};
    MemoryDesc memory{.preferredLocation = MemoryLocation::HostCached,
                      .requiredAccess = MemoryAccess::GPURead,
                      .preferredAccess = MemoryAccess::CPUWrite | MemoryAccess::CPURead};
    const auto desc = RenderMeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexData(AsBytes(positions), 0, memory).VertexCount(1).Build();
    EXPECT_EQ(desc.vertexData[0].memory.preferredLocation, MemoryLocation::HostCached);
    EXPECT_EQ(desc.vertexData[0].memory.preferredAccess,
              MemoryAccess::CPUWrite | MemoryAccess::CPURead);
}

TEST(MeshTests, RendererOwnsExactlyOneDevice) {
    Renderer renderer;
    EXPECT_EQ(renderer.GetRenderDevice(), nullptr);
    auto first = std::make_unique<TestDevice>();
    auto* owned = first.get();
    renderer.SetRenderDevice(std::move(first));
    EXPECT_EQ(renderer.GetRenderDevice(), owned);
    EXPECT_THROW(renderer.SetRenderDevice(std::make_unique<TestDevice>()), std::logic_error);
    EXPECT_EQ(renderer.GetRenderDevice(), owned);
    renderer.ResetRenderDevice();
    EXPECT_EQ(renderer.GetRenderDevice(), nullptr);
}

TEST(MeshTests, RendererRejectsWrongUsageAndUndersizedBuffers) {
    auto owned = std::make_unique<TestDevice>();
    auto& device = *owned;
    Renderer renderer;
    renderer.SetRenderDevice(std::move(owned));
    const auto wrongUsage = MakeBuffer(device, 128, BufferUsage::Index);
    auto invalidUsage = RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexBuffer(wrongUsage).VertexCount(1);
    EXPECT_THROW(renderer.CreateRenderMesh(invalidUsage), std::invalid_argument);

    const auto small = MakeBuffer(device, 8, BufferUsage::Vertex);
    auto tooSmall = RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexBuffer(small).VertexCount(1);
    EXPECT_THROW(renderer.CreateRenderMesh(tooSmall), std::invalid_argument);
}

TEST(MeshTests, RejectsDuplicateAttributeLocations) {
    auto builder = RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .Attribute("CUSTOM", VertexFormat::UInt, 0);
    EXPECT_THROW(builder.Build(), std::invalid_argument);
}

TEST(MeshTests, InvalidAndStaleRenderMeshHandlesAreRejected) {
    Renderer renderer;
    EXPECT_FALSE(renderer.IsValid(RenderMeshHandle{}));
    EXPECT_EQ(renderer.GetRenderMesh(RenderMeshHandle{}), nullptr);
    auto mesh = renderer.CreateRenderMesh(RenderMeshBuilder().Build());
    ASSERT_TRUE(renderer.IsValid(mesh));
    EXPECT_TRUE(renderer.DestroyRenderMesh(mesh));
    EXPECT_FALSE(renderer.IsValid(mesh));
    EXPECT_FALSE(renderer.DestroyRenderMesh(mesh));
}

TEST(MeshTests, DestroyingAReferencedDeviceBufferInvalidatesTheMesh) {
    auto owned = std::make_unique<TestDevice>();
    auto& device = *owned;
    Renderer renderer;
    renderer.SetRenderDevice(std::move(owned));
    const auto vertexBuffer = MakeBuffer(device, 36, BufferUsage::Vertex);
    const auto mesh = renderer.CreateRenderMesh(RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexBuffer(vertexBuffer).VertexCount(3));
    ASSERT_TRUE(renderer.IsValid(mesh));
    device.Destroy(vertexBuffer);
    EXPECT_FALSE(renderer.IsValid(mesh));
    EXPECT_EQ(renderer.GetRenderMesh(mesh), nullptr);
}

TEST(MeshTests, CpuVertexAndIndexDataAreCopiedAndValidated) {
    std::array<float, 9> vertices{};
    std::array<std::uint16_t, 3> indices{0, 1, 2};
    auto builder = RenderMeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexData(AsBytes(vertices)).IndexData(AsBytes(indices), IndexFormat::UInt16)
        .VertexCount(3).IndexCount(3);
    vertices[0] = 42.0f;
    const auto desc = builder.Build();
    float copiedValue{};
    std::memcpy(&copiedValue, desc.vertexData[0].bytes.data(), sizeof(float));
    EXPECT_EQ(copiedValue, 0.0f);
    Renderer renderer;
    const auto mesh = renderer.CreateRenderMesh(desc);
    ASSERT_TRUE(renderer.IsValid(mesh));
    EXPECT_EQ(renderer.GetRenderMeshUploadStatus(mesh), RenderMeshUploadStatus::UploadNotImplemented);
    ASSERT_TRUE(renderer.GetRenderMesh(mesh)->hasIndexData);
    EXPECT_EQ(renderer.GetRenderMesh(mesh)->indexData.size(), sizeof(indices));
}

TEST(MeshTests, RejectsBufferReferencesFromDifferentDevices) {
    auto owned = std::make_unique<TestDevice>();
    auto& deviceA = *owned;
    TestDevice deviceB;
    Renderer renderer;
    renderer.SetRenderDevice(std::move(owned));
    const auto vertex = MakeBuffer(deviceA, 12, BufferUsage::Vertex);
    const auto index = MakeBuffer(deviceB, 4, BufferUsage::Index);
    auto builder = RenderMeshBuilder().Attribute("P", VertexFormat::Float3, 0).VertexBuffer(vertex)
        .VertexCount(1).IndexBuffer(index).IndexCount(1);
    EXPECT_THROW(renderer.CreateRenderMesh(builder), std::invalid_argument);
}

TEST(MeshTests, DefaultMemorySettingsAreAlsoUsedByGenericBuffers) {
    const BufferDesc buffer{};
    EXPECT_EQ(buffer.memory.preferredLocation, MemoryLocation::DeviceLocal);
    EXPECT_EQ(buffer.memory.requiredAccess, MemoryAccess::GPURead);
}

TEST(MeshTests, SupportsRequestedRendererCreateRenderMeshBuilderCall) {
    auto owned = std::make_unique<TestDevice>();
    auto& device = *owned;
    Renderer renderer;
    renderer.SetRenderDevice(std::move(owned));
    const auto customBuffer = MakeBuffer(device, 1024 * 28, BufferUsage::Vertex);
    const RenderMeshHandle mesh = renderer.CreateRenderMesh(
        RenderMeshBuilder()
            .Attribute("POSITION", VertexFormat::Float3, 0)
            .Attribute("CUSTOM_DATA", VertexFormat::UInt4, 1)
            .VertexBuffer(customBuffer)
            .Topology(PrimitiveTopology::TriangleList)
            .VertexCount(1024));
    EXPECT_TRUE(renderer.IsValid(mesh));
    EXPECT_EQ(renderer.GetRenderMesh(mesh)->layout.attributes[1].semantic, "CUSTOM_DATA");
}
} // namespace
