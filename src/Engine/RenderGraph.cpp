#include <Lighthouse/Engine/Rendering/RenderGraph.hpp>

#include <stdexcept>

namespace Lighthouse {
namespace {
IRenderDevice& DeviceFrom(void* allocator) {
    if (!allocator) throw std::invalid_argument("Render graph execution requires a render device");
    return *static_cast<IRenderDevice*>(allocator);
}
}

void RenderGraphTexture::create(const Desc& desc, void* allocator) {
    handle = DeviceFrom(allocator).CreateImage(desc);
}

void RenderGraphTexture::destroy(const Desc&, void* allocator) {
    if (handle) DeviceFrom(allocator).Destroy(handle);
    handle = {};
}

void RenderGraphBuffer::create(const Desc& desc, void* allocator) {
    handle = DeviceFrom(allocator).CreateBuffer(desc);
}

void RenderGraphBuffer::destroy(const Desc&, void* allocator) {
    if (handle) DeviceFrom(allocator).Destroy(handle);
    handle = {};
}

RenderGraph::RenderGraph() : m_graph(std::make_unique<FrameGraph>()) {}
RenderGraph::~RenderGraph() = default;

void RenderGraph::Compile() {
    if (m_compiled) return;
    m_graph->compile();
    m_compiled = true;
}

void RenderGraph::Execute(IRenderDevice& device) {
    if (!m_compiled) throw std::logic_error("Compile the render graph before executing it");
    m_graph->execute(&device, &device);
}

void RenderGraph::Clear() {
    m_graph = std::make_unique<FrameGraph>();
    m_blackboard = FrameGraphBlackboard{};
    m_compiled = false;
}

} // namespace Lighthouse
