#include "Lighthouse/Engine/Rendering/Graph/IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include "Lighthouse/Engine/Rendering/RenderGraphComposer.hpp"
#include <Lighthouse/Engine/Core/Profiling.hpp>
#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>
#include <stdexcept>

namespace Lighthouse
{

struct RenderGraph::ExecutionState
{
  struct Pending
  {
    IRenderDevice* device;
    IRenderGraphAllocator* allocator;
    FenceHandle fence;
    std::vector<std::unique_ptr<IRenderCommandList>> commands;
    std::vector<ImageViewHandle> images;
    std::vector<BufferHandle> buffers;
    std::vector<std::function<void()>> retirements;
    void Release()
    {
      for (auto& release : retirements)
        release();
      commands.clear();
      for (auto image : images)
        allocator->FreeImage(image);
      for (auto buffer : buffers)
        allocator->FreeBuffer(buffer);
      device->DestroyFence(fence);
    }
  };
  std::vector<Pending> pending;
};
RenderGraph::RenderGraph()
    : m_execution(std::make_unique<ExecutionState>()),
      m_graph(std::make_unique<FrameGraph>())
{
}
RenderGraph::~RenderGraph()
{
  Drain();
}
void RenderGraph::Drain()
{
  if (!m_execution)
    return;
  for (auto& execution : m_execution->pending)
  {
    execution.device->WaitForFence(execution.fence);
    execution.Release();
  }
  m_execution->pending.clear();
}
RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
RenderGraph& RenderGraph::operator=(RenderGraph&& other) noexcept
{
  if (this == &other)
    return *this;
  Drain();
  using std::swap;
  swap(m_execution, other.m_execution);
  swap(m_passInfo, other.m_passInfo);
  swap(m_resourceInfo, other.m_resourceInfo);
  swap(m_hiddenFromInspector, other.m_hiddenFromInspector);
  swap(m_resourceNodes, other.m_resourceNodes);
  swap(m_names, other.m_names);
  swap(m_graph, other.m_graph);
  swap(m_blackboard, other.m_blackboard);
  swap(m_compiled, other.m_compiled);
  return *this;
}

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

void RenderGraph::Execute(RenderGraphComposer* renderer, IRenderGraphAllocator* allocator)
{
  LIGHTHOUSE_PROFILE_SCOPE("RenderGraph::Execute");
  if (!m_compiled)
    throw std::logic_error("Compile the render graph before executing it");

  // Imported host buffers and descriptor sets are reused by the next frame.
  // Their owners currently provide one set of storage, so retire the previous
  // execution before recording host writes into that storage.
  Drain();
  m_execution->pending.reserve(m_execution->pending.size() + 1);
  auto* device = renderer->GetRenderDevice();
  // FrameGraph releases resources at their last pass. Collect those releases
  // until every submitted use has completed, including command-list storage.
  struct DeferredAllocator final : IRenderGraphAllocator
  {
    IRenderGraphAllocator& source;
    std::vector<ImageViewHandle> images;
    std::vector<BufferHandle> buffers;
    explicit DeferredAllocator(IRenderGraphAllocator& source) : source(source)
    {
    }
    RenderGraphImageAllocation
    AllocateImage(const RenderGraphImageDesc& d) override
    {
      auto allocation = source.AllocateImage(d);
      images.push_back(allocation.view);
      return allocation;
    }
    RenderGraphBufferAllocation
    AllocateBuffer(const RenderGraphBufferDesc& d) override
    {
      auto allocation = source.AllocateBuffer(d);
      buffers.push_back(allocation.buffer);
      return allocation;
    }
    void FreeImage(ImageViewHandle) override {}
    void FreeBuffer(BufferHandle) override {}
  } deferred(*allocator);
  RenderGraphExecuteContext context(renderer, device);
  FenceHandle fence{};
  try
  {
    m_graph->execute(&context, &deferred);
    fence = device->CreateFence();
    device->GetQueue(0)->Submit(QueueSubmitInfo{}, fence);
    m_execution->pending.push_back(
        {device, allocator, fence, std::move(context.commandLists),
         std::move(deferred.images), std::move(deferred.buffers),
         std::move(context.retirements)});
  }
  catch (...)
  {
    device->WaitIdle();
    if (fence)
      device->DestroyFence(fence);
    for (auto& release : context.retirements)
      release();
    for (auto image : deferred.images)
      allocator->FreeImage(image);
    for (auto buffer : deferred.buffers)
      allocator->FreeBuffer(buffer);
    throw;
  }
}

void RenderGraph::Clear()
{
  Drain();
  if (!m_execution)
    m_execution = std::make_unique<ExecutionState>();
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
  auto cmdList = renderer->GetRenderDevice()->CreateCommandList(0);
  commandLists.push_back(std::move(cmdList));
  return commandLists.back().get();
}
} // namespace Lighthouse
