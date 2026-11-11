#include "NullSynchronizationDevice.hpp"
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <cassert>
using namespace Lighthouse;
namespace {
class TestRenderDevice final : public NullSynchronizationDevice {
public:
    std::vector<std::string> events;
    struct Commands final : IRenderCommandList {
        std::vector<std::string>& events;
        explicit Commands(std::vector<std::string>& events) : events(events) {}
        void Begin() override { events.push_back("begin"); }
        void End() override { events.push_back("end"); }
        void MemoryBarrier(const MemoryBarrierDesc& desc) override {
            assert(desc.srcStageMask == Flags<PipelineStage>(PipelineStage::ComputeShader));
            assert(desc.srcAccessMask == Flags<PipelineAccess>(PipelineAccess::ShaderStorageWrite));
            assert(desc.dstStageMask == Flags<PipelineStage>(PipelineStage::FragmentShader));
            assert(desc.dstAccessMask == Flags<PipelineAccess>(PipelineAccess::ShaderStorageRead));
            events.push_back("barrier");
        }
        void PushDebugGroup(std::string_view name, std::array<float,4>) override { events.emplace_back(name); }
        void PopDebugGroup() override { events.push_back("pop"); }
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
        void BlitImage(ImageHandle, ImageHandle, const BlitRegion&, ImageFilter) override {}
        void Clear() override {}
    };
    struct Queue final : IRenderQueue {
        TestRenderDevice& device;
        explicit Queue(TestRenderDevice& device) : device(device) {}
        Flags<RenderQueueCapability> GetCapabilities() const noexcept override { return {}; }
        void Submit(const QueueSubmitInfo& info, FenceHandle fence) override {
            assert(info.commandLists.size() == 1 && info.commandLists[0] == device.recorded);
            assert(fence);
            assert(info.waits.size() == device.expectedWaits);
            assert(info.signals.size() == device.expectedSignals);
            device.events.push_back("submit");
        }
        void Present(SwapchainHandle, std::span<const SemaphoreHandle>) override {}
        void WaitIdle() override {}
    } queue{*this};
    IRenderCommandList* recorded{};
    std::size_t expectedWaits{}, expectedSignals{};
    IRenderQueue* GetQueue(std::uint32_t) override { return &queue; }
    SwapchainHandle CreateSwapchain(SurfaceHandle, const SwapchainConfig&) override { return {}; }
    AcquiredSwapchainImage AcquireSwapchainImage(SwapchainHandle) override { return {}; }
    bool DestroySwapchain(SwapchainHandle) override { return false; }
    bool IsValid(SwapchainHandle) const noexcept override { return false; }
    ImageHandle CreateImage(const ImageDesc&) override { return {1, 1, DeviceId()}; }
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
    std::unique_ptr<IRenderCommandList>
    CreateCommandList(std::uint32_t queueIndex) override
    {
      events.push_back("create");
      auto commands = std::make_unique<Commands>(events);
      recorded = commands.get();
      return commands;
    }
    void DestroyImage(ImageHandle) override {}
    void DestroyImageView(ImageViewHandle) override {}
    void DestroyBuffer(BufferHandle) override {}
    void DestroyPipeline(GraphicsPipelineHandle) override {}
    void DestroyModule(ShaderModuleHandle) override {}
    void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle) override {}
    void DestroyDescriptorSet(DescriptorSetHandle) override {}
    void DestroyPipelineLayout(PipelineLayoutHandle) override {}
    void DestroySampler(SamplerHandle) override {}
    RenderCapabilities GetCapabilities() const override { return {}; }
    void WaitIdle() override {}
};

class OrderedView final : public IRenderView {
public:
    OrderedView(std::vector<int>& order, int id) : order(order), id(id) {}
    void BuildGraph(ViewBuildContext&) override { order.push_back(id); }
    std::vector<int>& order;
    int id;
};
}
namespace { void CheckGraphSubmission(); }
int main() {
    CheckGraphSubmission();
    spdlog::drop("RenderGraphSimpleAllocator");
    RenderGraphComposer renderer(std::make_unique<TestRenderDevice>());
    // The allocator currently registers a process-wide logger for each renderer.
    spdlog::drop("RenderGraphSimpleAllocator");
    RenderGraphComposer other(std::make_unique<TestRenderDevice>());
    std::vector<int> order;
    renderer.CreateView<OrderedView>(order, 1);
    auto first = renderer.AddLambdaTechnique([&](TechniqueBuildContext& ctx) {
        assert(&ctx.renderer == &renderer);
        order.push_back(3);
    });
    renderer.CreateView<OrderedView>(order, 2);
    renderer.AddLambdaTechnique([&](TechniqueBuildContext& ctx) {
        assert(&ctx.GetTechnique(first) == renderer.GetTechnique(first));
        order.push_back(4);
    });
    renderer.Compile();
    assert((order == std::vector<int>{1, 2, 3, 4}));
    assert(other.GetTechnique(first) == nullptr);
    assert(!other.RemoveTechnique(first));
    assert(renderer.RemoveTechnique(first));
    assert(renderer.GetTechnique(first) == nullptr);
    assert(!renderer.RemoveTechnique(first));
    auto replacement = renderer.AddLambdaTechnique([](TechniqueBuildContext&) {});
    assert(replacement.handle.index == first.handle.index);
    assert(replacement.handle.generation != first.handle.generation);
    TechniqueBuildContext context(renderer.GetRenderGraph(), renderer, *renderer.GetRenderDevice());
    bool rejected = false;
    try { context.GetTechnique(first); } catch (const std::logic_error&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { context.GetTechnique(other.AddLambdaTechnique([](TechniqueBuildContext&) {})); }
    catch (const std::logic_error&) { rejected = true; }
    assert(rejected);
    assert(renderer.Techniques().size() == 2);
    renderer.RemoveTechnique(renderer.Techniques()[1]);
    order.clear();
    renderer.Compile();
    assert((order == std::vector<int>{1, 2}));
}

namespace {
struct EmptyAllocator final : IRenderGraphAllocator {
    RenderGraphImageAllocation AllocateImage(const RenderGraphImageDesc&) override { return {}; }
    RenderGraphBufferAllocation AllocateBuffer(const RenderGraphBufferDesc&) override { return {}; }
    void FreeImage(ImageViewHandle) override {}
    void FreeBuffer(BufferHandle) override {}
};
void CheckGraphSubmission() {
    spdlog::drop("RenderGraphSimpleAllocator");
    auto device = std::make_unique<TestRenderDevice>();
    auto& gpu = *device;
    RenderGraphComposer composer(std::move(device));
    EmptyAllocator allocator;
    RenderGraph graph;
    auto setup = [](RenderGraph::Builder& b, FrameGraph::NoData&) { b.SetSideEffect(); };
    for (const auto* name : {"first", "second"})
        graph.AddPass(name, setup, [&, name](const auto&, auto&, RenderGraphExecutePassContext& ctx) {
            assert(ctx.commandList == gpu.recorded);
            gpu.events.push_back("execute");
            // Only the second callback requests a barrier, inside its debug group.
            if (std::string_view(name) == "second")
                ctx.commandList->MemoryBarrier({.srcStageMask = PipelineStage::ComputeShader,
                    .srcAccessMask = PipelineAccess::ShaderStorageWrite,
                    .dstStageMask = PipelineStage::FragmentShader,
                    .dstAccessMask = PipelineAccess::ShaderStorageRead});
            ctx.waits.push_back({});
            ctx.signals.push_back({});
            ctx.afterSubmit = [&] { gpu.events.push_back("after"); };
            ctx.Defer([&] { gpu.events.push_back("retire"); });
        });
    gpu.expectedWaits = gpu.expectedSignals = 2;
    graph.Compile();
    graph.Execute(&composer, &allocator);
    assert((gpu.events == std::vector<std::string>{"create", "begin", "first", "execute", "pop",
        "second", "execute", "barrier", "pop", "end", "submit", "after", "after"}));
    graph.Drain();
    assert(gpu.events[gpu.events.size()-2] == "retire" && gpu.events.back() == "retire");
    graph.Clear();
    gpu.events.clear();
    graph.AddPass("failure", setup, [&](const auto&, auto&, RenderGraphExecutePassContext& ctx) {
        ctx.Defer([&] { gpu.events.push_back("retire"); });
        ctx.afterSubmit = [&] { gpu.events.push_back("after"); };
        throw std::runtime_error("recording failed");
    });
    graph.Compile();
    bool rejected = false;
    try { graph.Execute(&composer, &allocator); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    assert((gpu.events == std::vector<std::string>{"create", "begin", "failure", "pop", "retire"}));
    graph.Clear();
    gpu.events.clear();
    gpu.expectedWaits = gpu.expectedSignals = 0;
    graph.Compile();
    graph.Execute(&composer, &allocator);
    assert((gpu.events == std::vector<std::string>{"create", "begin", "end", "submit"}));
    graph.Drain();
}
}
