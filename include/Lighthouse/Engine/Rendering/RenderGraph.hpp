#pragma once

#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <fg/Blackboard.hpp>
#include <fg/FrameGraph.hpp>
#include <fg/FrameGraphResource.hpp>

namespace Lighthouse {

using RenderGraphResource = FrameGraphResource;
using GraphImageHandle = RenderGraphResource;
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

struct ImportedRenderGraphTexture {
    using Desc = GraphTextureDesc;

    ImageHandle handle;
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
            return m_builder.create<ResourceType>(name, desc);
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
        explicit Builder(FrameGraph::Builder& builder) : m_builder(builder) {}
        FrameGraph::Builder& m_builder;
    };

    RenderGraph();
    ~RenderGraph();
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    template <typename Data = FrameGraph::NoData, typename Setup, typename Execute>
    const Data& AddPass(std::string_view name, Setup&& setup, Execute&& execute) {
        if (m_compiled) throw std::logic_error("Cannot add render passes after compilation");
        return m_graph->addCallbackPass<Data>(name,
            [setup = std::forward<Setup>(setup)](FrameGraph::Builder& builder, Data& data) mutable {
                Builder lighthouseBuilder(builder);
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
        return m_graph->import<ResourceType>(name, desc, std::move(resource));
    }

    void Compile();
    void Execute(IRenderDevice& device);
    void Clear();
    FrameGraphBlackboard& Blackboard() noexcept { return m_blackboard; }
    const FrameGraphBlackboard& Blackboard() const noexcept { return m_blackboard; }

private:
    std::unique_ptr<FrameGraph> m_graph;
    FrameGraphBlackboard m_blackboard;
    bool m_compiled{};
};

} // namespace Lighthouse
