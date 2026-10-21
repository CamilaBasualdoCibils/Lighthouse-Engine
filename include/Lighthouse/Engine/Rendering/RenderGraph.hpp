#pragma once

#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <string>
#include <vector>

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <fg/FrameGraphResource.hpp>

namespace Lighthouse {

using RenderGraphResource = FrameGraphResource;
using GraphImageHandle = RenderGraphResource;
inline constexpr GraphImageHandle InvalidGraphImage = -1;
using GraphTextureDesc = ImageDesc;
using GraphBufferDesc = BufferDesc;

struct RenderGraphTexture {
    using Desc = GraphTextureDesc;

    ImageHandle handle;
    void create(const Desc& desc, void* allocator);
    void destroy(const Desc& desc, void* allocator);
    void preRead(const Desc&, std::uint32_t, void*) {}
    void preWrite(const Desc&, std::uint32_t, void*) {}
};

struct RenderGraphBuffer {
    using Desc = GraphBufferDesc;

    BufferHandle handle;
    void create(const Desc& desc, void* allocator);
    void destroy(const Desc& desc, void* allocator);
    void preRead(const Desc&, std::uint32_t, void*) {}
    void preWrite(const Desc&, std::uint32_t, void*) {}
};

struct ImportedRenderGraphImage {
    using Desc = GraphTextureDesc;

    ImageHandle handle;
    ImageViewHandle view;
    void create(const Desc&, void*) {}
    void destroy(const Desc&, void*) {}
    void preRead(const Desc&, std::uint32_t, void*) {}
    void preWrite(const Desc&, std::uint32_t, void*) {}
};

struct ImportedRenderGraphBuffer {
    using Desc = GraphBufferDesc;

    BufferHandle handle;
    void create(const Desc&, void*) {}
    void destroy(const Desc&, void*) {}
    void preRead(const Desc&, std::uint32_t, void*) {}
    void preWrite(const Desc&, std::uint32_t, void*) {}
};

class RenderGraph final {
public:
    class Builder final {
        friend class RenderGraph;

    public:
        template <typename ResourceType>
        [[nodiscard]] RenderGraphResource Create(std::string_view name,
                                                  const typename ResourceType::Desc& desc) {
            return m_builder.create<ResourceType>(m_graph.KeepName(name), desc);
        }

        [[nodiscard]] RenderGraphResource Read(RenderGraphResource resource) {
            return m_builder.read(resource);
        }

        [[nodiscard]] RenderGraphResource Write(RenderGraphResource resource) {
            return m_builder.write(resource);
        }

        Builder& SetSideEffect() {
            m_builder.setSideEffect();
            return *this;
        }

    private:
        explicit Builder(RenderGraph& graph, FrameGraph::Builder& builder)
            : m_graph(graph), m_builder(builder) {}
        RenderGraph& m_graph;
        FrameGraph::Builder& m_builder;
    };

    RenderGraph();
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;
    RenderGraph(RenderGraph&&) noexcept;
    RenderGraph& operator=(RenderGraph&&) noexcept;

    template <typename Data = FrameGraph::NoData, typename Setup, typename Execute>
    const Data& AddPass(std::string_view name, Setup&& setup, Execute&& execute) {
        if (m_compiled) throw std::logic_error("Cannot add render passes after compilation");
        return m_graph->addCallbackPass<Data>(KeepName(name),
            [this, setup = std::forward<Setup>(setup)](FrameGraph::Builder& builder, Data& data) mutable {
                Builder lighthouseBuilder(*this, builder);
                setup(lighthouseBuilder, data);
            },
            [execute = std::forward<Execute>(execute)](
                const Data& data, FrameGraphPassResources& resources, void* context) mutable {
                execute(data, resources, *static_cast<IRenderDevice*>(context));
            });
    }

    template <typename ResourceType>
    [[nodiscard]] RenderGraphResource Import(std::string_view name,
                                              const typename ResourceType::Desc& desc,
                                              ResourceType resource) {
        if (m_compiled) throw std::logic_error("Cannot import render graph resources after compilation");
        return m_graph->import<ResourceType>(KeepName(name), desc, std::move(resource));
    }

    void Compile();
    void Execute(IRenderDevice& device);
    void Clear();
    void SetViewOutput(RenderViewHandle view, GraphImageHandle image) { m_viewOutputs.emplace_back(view, image); }
    GraphImageHandle GetViewOutput(RenderViewHandle view) const {
        for (const auto& [handle, image] : m_viewOutputs) if (handle == view) return image;
        throw std::logic_error("View output is missing");
    }
    FrameGraphBlackboard& Blackboard() noexcept { return m_blackboard; }
    const FrameGraphBlackboard& Blackboard() const noexcept { return m_blackboard; }

private:
    std::string_view KeepName(std::string_view name) {
        auto& owned = m_names.emplace_back(std::make_unique<std::string>(name));
        return *owned;
    }
    std::vector<std::unique_ptr<std::string>> m_names;
    std::unique_ptr<FrameGraph> m_graph;
    FrameGraphBlackboard m_blackboard;
    std::vector<std::pair<RenderViewHandle, GraphImageHandle>> m_viewOutputs;
    bool m_compiled{};
};

} // namespace Lighthouse
