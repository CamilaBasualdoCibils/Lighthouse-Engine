#include "Lighthouse/Engine/Rendering/Graph/IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>
#include "Lighthouse/Engine/Rendering/Renderer.hpp"
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

void RenderGraph::Execute(Renderer* renderer,IRenderGraphAllocator* allocator) {
    if (!m_compiled) throw std::logic_error("Compile the render graph before executing it");
    
    RenderGraphExecuteContext context(renderer, renderer->GetRenderDevice());
  
  
    m_graph->execute(&context, allocator); 
    renderer->GetRenderDevice()->WaitIdle();
    for (auto &cmdList : context.commandLists)
    {
    //device->DestroyCommandList(cmdList);
    }
}

void RenderGraph::Clear() {
    m_graph = std::make_unique<FrameGraph>();
    m_names.clear();
    m_blackboard = FrameGraphBlackboard{};
    m_compiled = false;
}

void ImportedWindowView::Resolve(const Desc &desc, void *context) {
  auto *ctx = static_cast<RenderGraphExecuteContext *>(context);


  auto acquired = handle->GetAcquiredImage();

  currentImage = acquired->image;
  currentImageView = acquired->view;
}
IRenderCommandList *RenderGraphExecuteContext::MakeCommandList() {
  auto cmdList =
      renderer->GetRenderDevice()->CreateCommandList(QueueType::Graphics);
  commandLists.push_back(std::move(cmdList));
  return commandLists.back().get();
}
} // namespace Lighthouse
