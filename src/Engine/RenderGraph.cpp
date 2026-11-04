#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>

#include <stdexcept>

namespace Lighthouse {

RenderGraph::RenderGraph() : m_graph(std::make_unique<FrameGraph>()) {}
RenderGraph::~RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

void RenderGraph::Compile() {
    if (m_compiled) return;
    m_graph->compile();
    m_compiled = true;
}

void RenderGraph::Execute(RenderGraphExecuteContext* context) {
    if (!m_compiled) throw std::logic_error("Compile the render graph before executing it");
    m_graph->execute(context, context->allocator); 
}

void RenderGraph::Clear() {
    m_graph = std::make_unique<FrameGraph>();
    m_names.clear();
    m_blackboard = FrameGraphBlackboard{};
    m_compiled = false;
}

} // namespace Lighthouse
