#pragma once

#include "IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Core/Handle.hpp"
#include "Lighthouse/Engine/Rendering/Graph/IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Rendering/IRenderCommandList.hpp"
#include "Lighthouse/Engine/Rendering/IRenderDevice.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <fg/FrameGraphResource.hpp>

namespace Lighthouse {
class Renderer;

using RenderGraphResource = FrameGraphResource;
constexpr RenderGraphResource kInvalidGraphResource = -1;

struct RenderGraphExecutePassContext {
  IRenderCommandList *commandList;
};
struct RenderGraphExecuteContext {
public:
  RenderGraphExecuteContext(Renderer *renderer, IRenderDevice *device)
      : renderer(renderer), device(device) {}
  Renderer *renderer;
  IRenderDevice *device;
  std::vector<std::unique_ptr<IRenderCommandList>> commandLists;

  IRenderCommandList *MakeCommandList();
};

struct RenderGraphImage {
  using Desc = RenderGraphImageDesc;
  ImageHandle handle;
  ImageViewHandle viewHandle;
  ImageSubresourceRange subresource;
  // ImageViewHandle viewHandle;
  void create(const Desc &desc, void *allocator) {
    IRenderGraphAllocator *alloc =
        static_cast<IRenderGraphAllocator *>(allocator);
    RenderGraphImageAllocation allocation = alloc->AllocateImage(desc);
    handle = allocation.image;
    viewHandle = allocation.view;
    subresource = allocation.subresource;
  }
  void destroy(const Desc &desc, void *allocator) {
    IRenderGraphAllocator *alloc =
        static_cast<IRenderGraphAllocator *>(allocator);
    alloc->FreeImage(viewHandle);
    handle = {};
    viewHandle = {};
    subresource = {};
  }
  void preRead(const Desc &, std::uint32_t, void *) {}
  void preWrite(const Desc &, std::uint32_t, void *) {}
};
struct RenderGraphBuffer {
  using Desc = RenderGraphBufferDesc;
  BufferHandle handle;
  // ImageViewHandle viewHandle;
  void create(const Desc &desc, void *allocator) {
    IRenderGraphAllocator *alloc =
        static_cast<IRenderGraphAllocator *>(allocator);
    RenderGraphBufferAllocation allocation = alloc->AllocateBuffer(desc);

  }
  void destroy(const Desc &desc, void *allocator) {
    IRenderGraphAllocator *alloc =
        static_cast<IRenderGraphAllocator *>(allocator);
    alloc->FreeBuffer(handle);
    handle = {};
  }
  void preRead(const Desc &, std::uint32_t, void *) {}
  void preWrite(const Desc &, std::uint32_t, void *) {}
};
/* struct RenderGraphBuffer {
    using Desc = GraphBufferDesc;

    BufferHandle handle;
    void create(const Desc& desc, void* allocator);
    void destroy(const Desc& desc, void* allocator);
    void preRead(const Desc&, std::uint32_t, void*) {}
    void preWrite(const Desc&, std::uint32_t, void*) {}
}; */

struct ImportedRenderGraphImage {
  struct Desc {
    uint32_t width{};
    uint32_t height{};
    ImageFormat format;
    Flags<ImageUsage> usage{};
  };
  ImageViewHandle viewHandle;
  ImageHandle handle;
  ImageSubresourceRange subresource;
  void create(const Desc &, void *) {}
  void destroy(const Desc &, void *) {}
  void preRead(const Desc &, std::uint32_t, void *) {}
  void preWrite(const Desc &, std::uint32_t, void *) {}
};
struct ImportedRenderGraphBuffer {
  struct Desc {
    uint64_t size{};
    Flags<BufferUsage> usage{};
  };
  BufferHandle handle;
  void create(const Desc &, void *) {}
  void destroy(const Desc &, void *) {}
  void preRead(const Desc &, std::uint32_t, void *) {}
  void preWrite(const Desc &, std::uint32_t, void *) {}
};
class WindowView;
struct ImportedWindowView {
  struct Desc {};
  ImportedWindowView() = default;
  ImportedWindowView(WindowView *handle) : handle(handle) {}

  WindowView *handle{};
  ImageHandle currentImage;
  ImageViewHandle currentImageView;
  void create(const Desc & desc, void *context) { Resolve(desc, context); }
  void destroy(const Desc &, void *context) {}
  void preRead(const Desc &desc, uint32_t, void *context) {
    Resolve(desc, context);
  }

  void preWrite(const Desc &desc, uint32_t, void *context) {
    Resolve(desc, context);
  }

  void Resolve(const Desc &desc, void *context);
};
/* struct ImportedRenderGraphBuffer {
    using Desc = GraphBufferDesc;

    BufferHandle handle;
    void create(const Desc&, void*) {}
    void destroy(const Desc&, void*) {}
    void preRead(const Desc&, std::uint32_t, void*) {}
    void preWrite(const Desc&, std::uint32_t, void*) {}
}; */

class RenderGraph final {
public:
  struct ResourceInfo {
    RenderGraphResource handle;
    RenderGraphResource resource;
    std::string name;
    std::uint32_t version{1};
    bool imported{};
    bool hidden{};
  };
  struct PassInfo {
    std::size_t id;
    std::string name;
    std::vector<RenderGraphResource> creates, reads, writes;
    bool hidden{};
  };
  struct InspectionData {
    std::vector<PassInfo> passes;
    std::vector<ResourceInfo> resources;
  };
  class InspectionScope {
    friend class RenderGraph;
    explicit InspectionScope(RenderGraph& graph)
        : m_graph(graph), m_previous(graph.m_hiddenFromInspector) {
      graph.m_hiddenFromInspector = true;
    }
  public:
    ~InspectionScope() { m_graph.m_hiddenFromInspector = m_previous; }
    InspectionScope(const InspectionScope&) = delete;
    InspectionScope& operator=(const InspectionScope&) = delete;
  private:
    RenderGraph& m_graph;
    bool m_previous;
  };
  // Scope only affects inspection metadata, never graph dependencies/execution.
  [[nodiscard]] InspectionScope HideFromInspector() { return InspectionScope(*this); }
  [[nodiscard]] InspectionData GetInspectionData(bool includeHidden = false) const;

  class Builder final {
    friend class RenderGraph;

  public:
    template <typename ResourceType>
    [[nodiscard]] RenderGraphResource
    Create(std::string_view name, const typename ResourceType::Desc &desc) {
      return m_graph.RecordCreate(m_pass,
          m_builder.create<ResourceType>(m_graph.KeepName(name), desc), name);
    }

    RenderGraphResource Read(RenderGraphResource resource,uint32_t flags = 0) {
      const auto result = m_builder.read(resource, flags);
      m_graph.RecordRead(m_pass, result);
      return result;
    }

    [[nodiscard]] RenderGraphResource Write(RenderGraphResource resource, uint32_t flags = 0) {
      return m_graph.RecordWrite(m_pass, resource, m_builder.write(resource, flags));
    }

    Builder &SetSideEffect() {
      m_builder.setSideEffect();
      return *this;
    }

  private:
    explicit Builder(RenderGraph &graph, FrameGraph::Builder &builder, std::size_t pass)
        : m_graph(graph), m_builder(builder), m_pass(pass) {}
    RenderGraph &m_graph;
    FrameGraph::Builder &m_builder;
    std::size_t m_pass;
  };

  RenderGraph();
  ~RenderGraph();
  RenderGraph(const RenderGraph &) = delete;
  RenderGraph &operator=(const RenderGraph &) = delete;
  RenderGraph(RenderGraph &&) noexcept;
  RenderGraph &operator=(RenderGraph &&) noexcept;

  template <typename Data = FrameGraph::NoData, typename Setup,
            typename Execute>
    requires(
        std::is_invocable_v<Setup, Builder &, Data &> &&
        std::is_invocable_v<Execute, const Data &, FrameGraphPassResources &,
                            RenderGraphExecutePassContext &>)
  const Data &AddPass(std::string_view name, Setup &&setup, Execute &&execute) {
    if (m_compiled)
      throw std::logic_error("Cannot add render passes after compilation");
    const auto pass = m_passInfo.size();
    m_passInfo.push_back({pass, std::string(name), {}, {}, {}, m_hiddenFromInspector});
    return m_graph->addCallbackPass<Data>(
        KeepName(name),
        [this, pass, setup = std::forward<Setup>(setup)](FrameGraph::Builder &builder,
                                                   Data &data) mutable {
          Builder lighthouseBuilder(*this, builder, pass);
          setup(lighthouseBuilder, data);
        },
        [execute = std::forward<Execute>(execute)](
            const Data &data, FrameGraphPassResources &resources,
            void *context) mutable {
          RenderGraphExecuteContext *executeContext =
              static_cast<RenderGraphExecuteContext *>(context);
          assert(executeContext);
          RenderGraphExecutePassContext executePassContext{
              .commandList = executeContext->MakeCommandList()};

          execute(data, resources, executePassContext);

          executeContext->device->Submit(*executePassContext.commandList);
          // FrameGraph can destroy transient resources as soon as this callback
          // returns. Complete their GPU use first on every backend; asynchronous
          // submission will require fence-based deferred resource retirement.
          executeContext->device->WaitIdle();
        });
  }

  template <typename ResourceType>
  [[nodiscard]] RenderGraphResource
  Import(std::string_view name, const typename ResourceType::Desc &desc,
         ResourceType resource) {
    if (m_compiled)
      throw std::logic_error(
          "Cannot import render graph resources after compilation");
    return TrackResource(m_graph->import<ResourceType>(
        KeepName(name), desc, std::move(resource)), name, true);
  }

  void Compile();
  void Execute(Renderer *device, IRenderGraphAllocator *allocator);
  void Clear();
  // Latest handle per resource, including imports, in first-declaration order.
  [[nodiscard]] const std::vector<RenderGraphResource>& GetResourceNodes() const noexcept {
    return m_resourceNodes;
  }
  template <typename T>
  [[nodiscard]] const typename T::Desc &
  getDescriptor(RenderGraphResource id) const {
    return m_graph->getDescriptor<T>(id);
  }
  std::string Dot() const {
    if (!m_graph)
      return "";
    std::stringstream ss;
    ss << *m_graph;
    return ss.str();
  }
  FrameGraphBlackboard &Blackboard() noexcept { return m_blackboard; }
  const FrameGraphBlackboard &Blackboard() const noexcept {
    return m_blackboard;
  }

private:
  static void AddUnique(std::vector<RenderGraphResource>& handles, RenderGraphResource handle);
  RenderGraphResource RecordCreate(std::size_t pass, RenderGraphResource handle, std::string_view name);
  void RecordRead(std::size_t pass, RenderGraphResource handle);
  RenderGraphResource RecordWrite(std::size_t pass, RenderGraphResource previous, RenderGraphResource written);
  RenderGraphResource TrackResource(RenderGraphResource handle, std::string_view name, bool imported);
  std::vector<PassInfo> m_passInfo;
  std::vector<ResourceInfo> m_resourceInfo;
  bool m_hiddenFromInspector{};
  std::vector<RenderGraphResource> m_resourceNodes;
  std::string_view KeepName(std::string_view name) {
    auto &owned = m_names.emplace_back(std::make_unique<std::string>(name));
    return *owned;
  }
  std::vector<std::unique_ptr<std::string>> m_names;
  std::unique_ptr<FrameGraph> m_graph;
  FrameGraphBlackboard m_blackboard;
  bool m_compiled{};
};

} // namespace Lighthouse
