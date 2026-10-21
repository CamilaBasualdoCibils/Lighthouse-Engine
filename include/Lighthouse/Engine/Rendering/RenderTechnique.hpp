#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <stdexcept>
#include <type_traits>

#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraph.hpp>

namespace Lighthouse {

class IRenderView;

template <typename T> struct TechniqueRef {
    RenderTechniqueHandle handle;
    explicit operator bool() const noexcept { return static_cast<bool>(handle); }
};

class TechniqueBuildContext {
public:
    TechniqueBuildContext(RenderGraph& graph, IRenderView& view, RenderGraph::Builder* builder = nullptr)
        : graph(graph), view(view), m_builder(builder) {}

    RenderGraph& graph;
    IRenderView& view;
    template <typename T> T* GetParentViewAs() noexcept;
    template <typename T> const T* GetParentViewAs() const noexcept;
    template <typename T> const T& GetTechnique(TechniqueRef<T> ref) const;
    RenderGraph::Builder& Builder() const {
        if (!m_builder) throw std::logic_error("No render graph pass is being built");
        return *m_builder;
    }

private:
    RenderGraph::Builder* m_builder{};
};

struct TechniqueExecuteContext {
    FrameGraphPassResources& resources;
    IRenderDevice& device;
};

class IRenderTechnique {
public:
    virtual ~IRenderTechnique() = default;
    virtual void BuildGraph(TechniqueBuildContext& context) = 0;
};

template <typename Data> class LambdaTechnique final : public IRenderTechnique {
public:
    using Setup = std::function<void(Data&, TechniqueBuildContext&)>;
    using Execute = std::function<void(const Data&, TechniqueExecuteContext&)>;

    LambdaTechnique(Setup setup, Execute execute)
        : m_setup(std::move(setup)), m_execute(std::move(execute)) {}

    Data& GetPassData() noexcept { return m_passData; }
    const Data& GetPassData() const noexcept { return m_passData; }

    void BuildGraph(TechniqueBuildContext& ctx) override {
        const auto& pass = ctx.graph.AddPass<Data>("lambda technique",
            [this, &ctx](RenderGraph::Builder& builder, Data& data) {
                TechniqueBuildContext setupContext(ctx.graph, ctx.view, &builder);
                m_setup(data, setupContext);
            },
            [execute = m_execute](const Data& data, FrameGraphPassResources& resources,
                                  IRenderDevice& device) {
                TechniqueExecuteContext executeContext{resources, device};
                execute(data, executeContext);
            });
        m_passData = pass;
    }

private:
    Setup m_setup;
    Execute m_execute;
    Data m_passData{};
};

} // namespace Lighthouse
