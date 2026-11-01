#pragma once

#include "IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Rendering/Graph/IRenderGraphAllocator.hpp"
#include "Lighthouse/Engine/Rendering/IRenderCommandList.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
#include <cstdint>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <fg/FrameGraphResource.hpp>

namespace Lighthouse {

using RenderGraphResource = FrameGraphResource;
constexpr RenderGraphResource kInvalidGraphResource = -1;

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
    Flags<ImageUsage> usage{ImageUsage::None};
  };
  ImageViewHandle viewHandle;
  ImageHandle handle;
  ImageSubresourceRange subresource;
  void create(const Desc &, void *) {}
  void destroy(const Desc &, void *) {}
  void preRead(const Desc &, std::uint32_t, void *) {}
  void preWrite(const Desc &, std::uint32_t, void *) {}
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
  class Builder final {
    friend class RenderGraph;

  public:
    template <typename ResourceType>
    [[nodiscard]] RenderGraphResource
    Create(std::string_view name, const typename ResourceType::Desc &desc) {
      return m_builder.create<ResourceType>(m_graph.KeepName(name), desc);
    }

    RenderGraphResource Read(RenderGraphResource resource) {
      return m_builder.read(resource);
    }

    [[nodiscard]] RenderGraphResource Write(RenderGraphResource resource) {
      return m_builder.write(resource);
    }

    Builder &SetSideEffect() {
      m_builder.setSideEffect();
      return *this;
    }

  private:
    explicit Builder(RenderGraph &graph, FrameGraph::Builder &builder)
        : m_graph(graph), m_builder(builder) {}
    RenderGraph &m_graph;
    FrameGraph::Builder &m_builder;
  };

  RenderGraph();
  ~RenderGraph();
  RenderGraph(const RenderGraph &) = delete;
  RenderGraph &operator=(const RenderGraph &) = delete;
  RenderGraph(RenderGraph &&) noexcept;
  RenderGraph &operator=(RenderGraph &&) noexcept;

  template <typename Data = FrameGraph::NoData, typename Setup,
            typename Execute>
  const Data &AddPass(std::string_view name, Setup &&setup, Execute &&execute) {
    if (m_compiled)
      throw std::logic_error("Cannot add render passes after compilation");
    return m_graph->addCallbackPass<Data>(
        KeepName(name),
        [this, setup = std::forward<Setup>(setup)](FrameGraph::Builder &builder,
                                                   Data &data) mutable {
          Builder lighthouseBuilder(*this, builder);
          setup(lighthouseBuilder, data);
        },
        [execute = std::forward<Execute>(execute)](
            const Data &data, FrameGraphPassResources &resources,
            void *context) mutable {
          execute(data, resources, *static_cast<IRenderCommandList *>(context));
        });
  }

  template <typename ResourceType>
  [[nodiscard]] RenderGraphResource
  Import(std::string_view name, const typename ResourceType::Desc &desc,
         ResourceType resource) {
    if (m_compiled)
      throw std::logic_error(
          "Cannot import render graph resources after compilation");
    return m_graph->import<ResourceType>(KeepName(name), desc,
                                         std::move(resource));
  }

  void Compile();
  void Execute(IRenderGraphAllocator &allocator);
  void Clear();
  template <typename T>
  [[nodiscard]] const typename T::Desc &
  getDescriptor(RenderGraphResource id) const
  {
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
