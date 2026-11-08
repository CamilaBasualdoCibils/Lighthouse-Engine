#include "NullSynchronizationDevice.hpp"
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <cassert>
using namespace Lighthouse;
namespace {
class TestRenderDevice final : public NullSynchronizationDevice {
public:
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
      return {};
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
int main() {
    Renderer renderer(std::make_unique<TestRenderDevice>());
    // The allocator currently registers a process-wide logger for each renderer.
    spdlog::drop("RenderGraphSimpleAllocator");
    Renderer other(std::make_unique<TestRenderDevice>());
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
