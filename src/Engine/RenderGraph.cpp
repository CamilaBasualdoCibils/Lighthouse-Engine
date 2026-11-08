#include <Lighthouse/Engine/Core/Profiling.hpp>
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

void RenderGraph::AddUnique(std::vector<RenderGraphResource>& handles, RenderGraphResource handle) {
    if (std::find(handles.begin(), handles.end(), handle) == handles.end()) handles.push_back(handle);
}
RenderGraphResource RenderGraph::TrackResource(RenderGraphResource handle, std::string_view name, bool imported) {
    AddUnique(m_resourceNodes, handle);
    m_resourceInfo.push_back({handle, handle, std::string(name), 1, imported, m_hiddenFromInspector});
    return handle;
}
RenderGraphResource RenderGraph::RecordCreate(std::size_t pass, RenderGraphResource handle, std::string_view name) {
    TrackResource(handle, name, false);
    AddUnique(m_passInfo[pass].creates, handle);
    return handle;
}
void RenderGraph::RecordRead(std::size_t pass, RenderGraphResource handle) {
    AddUnique(m_passInfo[pass].reads, handle);
}
RenderGraphResource RenderGraph::RecordWrite(std::size_t pass, RenderGraphResource previous, RenderGraphResource written) {
    if (previous != written) {
        const auto old = std::find_if(m_resourceInfo.begin(), m_resourceInfo.end(),
                                     [previous](const auto& info) { return info.handle == previous; });
        if (old == m_resourceInfo.end()) throw std::logic_error("Untracked render graph resource");
        auto info = *old;
        info.handle = written;
        ++info.version;
        info.hidden |= m_hiddenFromInspector;
        m_resourceInfo.push_back(std::move(info));
        // FrameGraph write-renaming implicitly reads the previous version.
        RecordRead(pass, previous);
    }
    AddUnique(m_passInfo[pass].writes, written);
    const auto found = std::find(m_resourceNodes.begin(), m_resourceNodes.end(), previous);
    if (found == m_resourceNodes.end()) throw std::logic_error("Untracked render graph resource");
    *found = written;
    return written;
}
RenderGraph::InspectionData RenderGraph::GetInspectionData(bool includeHidden) const {
    InspectionData data;
    for (const auto& resource : m_resourceInfo)
        if (includeHidden || !resource.hidden) data.resources.push_back(resource);
    for (const auto& pass : m_passInfo) {
        if (!includeHidden && pass.hidden) continue;
        auto visible = pass;
        auto filter = [&](auto& accesses) {
            std::erase_if(accesses, [&](auto handle) {
                return std::none_of(data.resources.begin(), data.resources.end(),
                                    [handle](const auto& resource) { return resource.handle == handle; });
            });
        };
        filter(visible.creates); filter(visible.reads); filter(visible.writes);
        data.passes.push_back(std::move(visible));
    }
    return data;
}

void RenderGraph::Compile() {
    LIGHTHOUSE_PROFILE_SCOPE("RenderGraph::Compile");
    if (m_compiled) return;
    m_graph->compile();
    m_compiled = true;
}

void RenderGraph::Execute(Renderer* renderer,IRenderGraphAllocator* allocator) {
    LIGHTHOUSE_PROFILE_SCOPE("RenderGraph::Execute");
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
    m_resourceNodes.clear();
    m_passInfo.clear();
    m_resourceInfo.clear();
    m_hiddenFromInspector = false;
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
