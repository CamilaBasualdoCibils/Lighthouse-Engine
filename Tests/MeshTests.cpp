#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

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
    ShaderModuleHandle CreateShaderModule(const ShaderModuleDesc&) override { return {}; }
    GraphicsPipelineHandle CreatePipeline(const GraphicsPipelineDesc&) override { return {}; }
    std::unique_ptr<IRenderCommandList> CreateCommandList(QueueType) override { return {}; }
    void Destroy(ImageHandle) override {}
    void Destroy(ImageViewHandle) override {}
    void Destroy(BufferHandle handle) override { m_buffers.erase(handle); }
    void Destroy(GraphicsPipelineHandle) override {}
    void Destroy(ShaderModuleHandle) override {}
    RenderCapabilities GetCapabilities() const override { return {}; }
    void Submit(SwapchainHandle, const IRenderCommandList&) override {}

private:
    std::uint32_t m_next{1};
    struct HandleHash { std::size_t operator()(BufferHandle handle) const noexcept { return handle.index; } };
    std::unordered_map<BufferHandle, BufferMetadata, HandleHash> m_buffers;
};

BufferHandle MakeBuffer(TestDevice& device, std::uint64_t bytes, BufferUsage usage) {
    return device.CreateBuffer(BufferDesc{.size = bytes, .usage = usage});
}

TEST(MeshTests, PositionOnlyMeshDoesNotRequireOtherSemantics) {
    const auto desc = MeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexCount(3).Build();
    EXPECT_EQ(desc.layout.attributes.size(), 1u);
    EXPECT_EQ(desc.layout.bindings.front().stride, 12u);
}

TEST(MeshTests, SupportsArbitraryCustomSemanticsAndIntegerFormats) {
    const auto desc = MeshBuilder().Attribute("CUSTOM_DATA", VertexFormat::UInt4, 0)
        .Attribute("JOINTS0", VertexFormat::Int4, 1).Build();
    EXPECT_EQ(desc.layout.attributes[0].semantic, "CUSTOM_DATA");
    EXPECT_EQ(desc.layout.attributes[1].semantic, "JOINTS0");
    EXPECT_EQ(VertexFormatByteSize(VertexFormat::SNorm8x4), 4u);
}

TEST(MeshTests, BuildsInterleavedOffsetsAndAutomaticStride) {
    struct Vertex { float position[3]; float normal[3]; float uv[2]; };
    const auto desc = MeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .Attribute("NORMAL", VertexFormat::Float3, 1)
        .Attribute("TEXCOORD0", VertexFormat::Float2, 2).Build();
    ASSERT_EQ(desc.layout.attributes.size(), 3u);
    EXPECT_EQ(desc.layout.attributes[0].offset, 0u);
    EXPECT_EQ(desc.layout.attributes[1].offset, 12u);
    EXPECT_EQ(desc.layout.attributes[2].offset, 24u);
    EXPECT_EQ(desc.layout.bindings[0].stride, sizeof(Vertex));
}

TEST(MeshTests, SupportsMultipleIndependentVertexBindings) {
    auto desc = MeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0, 0)
        .Attribute("NORMAL", VertexFormat::Float3, 1, 1)
        .VertexBuffer({0, 1}, 0).VertexBuffer({1, 1}, 1).VertexCount(8).Build();
    ASSERT_EQ(desc.layout.bindings.size(), 2u);
    EXPECT_EQ(desc.vertexBuffers[1].binding, 1u);
}

TEST(MeshTests, ExplicitOffsetsAndStridesArePreservedAndValidated) {
    const auto desc = MeshBuilder().Attribute("A", VertexFormat::Float3, 3, 0, 8)
        .VertexStride(24).Build();
    EXPECT_EQ(desc.layout.attributes[0].offset, 8u);
    EXPECT_EQ(desc.layout.bindings[0].stride, 24u);
    EXPECT_THROW(MeshBuilder().Attribute("A", VertexFormat::Float4, 0, 0, 8).VertexStride(20).Build(), std::invalid_argument);
}

TEST(MeshTests, HandlesIndexedAndNonIndexedMeshes) {
    const auto nonIndexed = MeshBuilder().Attribute("P", VertexFormat::Float3, 0).VertexCount(3).Build();
    EXPECT_FALSE(nonIndexed.indexBuffer.has_value());
    const auto indexed = MeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexCount(3).IndexBuffer({5, 1}).IndexCount(3)
        .VertexBuffer({4, 1}).Build();
    EXPECT_EQ(indexed.indexBuffer->format, IndexFormat::UInt32);
    EXPECT_EQ(indexed.topology, PrimitiveTopology::TriangleList);
}

TEST(MeshTests, ValidatesPerInstanceDataAgainstInstanceCount) {
    std::array<float, 12> instances{};
    auto builder = MeshBuilder().Attribute("TRANSFORM", VertexFormat::Float3, 0)
        .InputRate(VertexInputRate::PerInstance).VertexData(std::span<const float>(instances))
        .InstanceCount(4).VertexCount(100);
    const auto desc = builder.Build();
    EXPECT_EQ(desc.vertexData[0].bytes.size(), sizeof(instances));
    EXPECT_EQ(desc.layout.bindings[0].inputRate, VertexInputRate::PerInstance);
}

TEST(MeshTests, DefaultsCpuDataMemoryToDeviceLocalGpuRead) {
    std::array<float, 3> positions{};
    const auto desc = MeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
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
    const auto desc = MeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexData(AsBytes(positions), 0, memory).VertexCount(1).Build();
    EXPECT_EQ(desc.vertexData[0].memory.preferredLocation, MemoryLocation::HostCached);
    EXPECT_EQ(desc.vertexData[0].memory.preferredAccess,
              MemoryAccess::CPUWrite | MemoryAccess::CPURead);
}

TEST(MeshTests, RendererRejectsWrongUsageAndUndersizedBuffers) {
    TestDevice device;
    Renderer renderer;
    renderer.SetRenderDevice(&device);
    const auto wrongUsage = MakeBuffer(device, 128, BufferUsage::Index);
    auto invalidUsage = MeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexBuffer(wrongUsage).VertexCount(1);
    EXPECT_THROW(renderer.CreateMesh(invalidUsage), std::invalid_argument);

    const auto small = MakeBuffer(device, 8, BufferUsage::Vertex);
    auto tooSmall = MeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexBuffer(small).VertexCount(1);
    EXPECT_THROW(renderer.CreateMesh(tooSmall), std::invalid_argument);
}

TEST(MeshTests, RejectsDuplicateAttributeLocations) {
    auto builder = MeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .Attribute("CUSTOM", VertexFormat::UInt, 0);
    EXPECT_THROW(builder.Build(), std::invalid_argument);
}

TEST(MeshTests, InvalidAndStaleMeshHandlesAreRejected) {
    Renderer renderer;
    EXPECT_FALSE(renderer.IsValid(MeshHandle{}));
    EXPECT_EQ(renderer.GetMeshDesc(MeshHandle{}), nullptr);
    auto mesh = renderer.CreateMesh(MeshBuilder().Build());
    ASSERT_TRUE(renderer.IsValid(mesh));
    EXPECT_TRUE(renderer.DestroyMesh(mesh));
    EXPECT_FALSE(renderer.IsValid(mesh));
    EXPECT_FALSE(renderer.DestroyMesh(mesh));
}

TEST(MeshTests, DestroyingAReferencedDeviceBufferInvalidatesTheMesh) {
    TestDevice device;
    Renderer renderer;
    renderer.SetRenderDevice(&device);
    const auto vertexBuffer = MakeBuffer(device, 36, BufferUsage::Vertex);
    const auto mesh = renderer.CreateMesh(MeshBuilder().Attribute("P", VertexFormat::Float3, 0)
        .VertexBuffer(vertexBuffer).VertexCount(3));
    ASSERT_TRUE(renderer.IsValid(mesh));
    device.Destroy(vertexBuffer);
    EXPECT_FALSE(renderer.IsValid(mesh));
    EXPECT_EQ(renderer.GetMeshDesc(mesh), nullptr);
}

TEST(MeshTests, CpuVertexAndIndexDataAreCopiedAndValidated) {
    std::array<float, 9> vertices{};
    std::array<std::uint16_t, 3> indices{0, 1, 2};
    auto builder = MeshBuilder().Attribute("POSITION", VertexFormat::Float3, 0)
        .VertexData(AsBytes(vertices)).IndexData(AsBytes(indices), IndexFormat::UInt16)
        .VertexCount(3).IndexCount(3);
    vertices[0] = 42.0f;
    const auto desc = builder.Build();
    float copiedValue{};
    std::memcpy(&copiedValue, desc.vertexData[0].bytes.data(), sizeof(float));
    EXPECT_EQ(copiedValue, 0.0f);
    Renderer renderer;
    const auto mesh = renderer.CreateMesh(desc);
    ASSERT_TRUE(renderer.IsValid(mesh));
    EXPECT_EQ(renderer.GetMeshUploadStatus(mesh), MeshUploadStatus::UploadNotImplemented);
    ASSERT_TRUE(renderer.GetMeshDesc(mesh)->hasIndexData);
    EXPECT_EQ(renderer.GetMeshDesc(mesh)->indexData.size(), sizeof(indices));
}

TEST(MeshTests, RejectsBufferReferencesFromDifferentDevices) {
    TestDevice deviceA, deviceB;
    Renderer renderer;
    renderer.SetRenderDevice(&deviceA);
    const auto vertex = MakeBuffer(deviceA, 12, BufferUsage::Vertex);
    const auto index = MakeBuffer(deviceB, 4, BufferUsage::Index);
    auto builder = MeshBuilder().Attribute("P", VertexFormat::Float3, 0).VertexBuffer(vertex)
        .VertexCount(1).IndexBuffer(index).IndexCount(1);
    EXPECT_THROW(renderer.CreateMesh(builder), std::invalid_argument);
}

TEST(MeshTests, DefaultMemorySettingsAreAlsoUsedByGenericBuffers) {
    const BufferDesc buffer{};
    EXPECT_EQ(buffer.memory.preferredLocation, MemoryLocation::DeviceLocal);
    EXPECT_EQ(buffer.memory.requiredAccess, MemoryAccess::GPURead);
}

TEST(MeshTests, SupportsRequestedRendererCreateMeshBuilderCall) {
    TestDevice device;
    Renderer renderer;
    renderer.SetRenderDevice(&device);
    const auto customBuffer = MakeBuffer(device, 1024 * 28, BufferUsage::Vertex);
    const MeshHandle mesh = renderer.CreateMesh(
        MeshBuilder()
            .Attribute("POSITION", VertexFormat::Float3, 0)
            .Attribute("CUSTOM_DATA", VertexFormat::UInt4, 1)
            .VertexBuffer(customBuffer)
            .Topology(PrimitiveTopology::TriangleList)
            .VertexCount(1024));
    EXPECT_TRUE(renderer.IsValid(mesh));
    EXPECT_EQ(renderer.GetMeshDesc(mesh)->layout.attributes[1].semantic, "CUSTOM_DATA");
}
} // namespace
