#pragma once

#include "Lighthouse/Engine/Rendering/IRenderDevice.hpp"
#include <cstdint>
#include <functional>
#include <utility>
#include <stdexcept>
#include <type_traits>

#include <Lighthouse/Engine/Core/Handle.hpp>
#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>

namespace Lighthouse {

class Renderer;

template <typename T> struct TechniqueRef {
    RenderTechniqueHandle handle;
    explicit operator bool() const noexcept { return static_cast<bool>(handle); }
};

class TechniqueBuildContext {
public:
    TechniqueBuildContext(RenderGraph& graph, Renderer& renderer, IRenderDevice& device, RenderGraph::Builder* builder = nullptr)
        : graph(graph), renderer(renderer), device(device), m_builder(builder) {}

    RenderGraph& graph;
    Renderer& renderer;
    IRenderDevice& device;
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
/* template <typename Data>
class TRenderTechnique :public IRenderTechnique
{
   Data& GetPassData() noexcept { return m_passData; }
    const Data& GetPassData() const noexcept { return m_passData; }

    protected:
    void SetPassData(const Data& data) { m_passData = data; }
    private:
        Data m_passData{};
};*/
 class LambdaTechnique final : public IRenderTechnique {
public:
using Build = std::function<void(TechniqueBuildContext&)>;
    /* using Setup = std::function<void(Data&, TechniqueBuildContext&)>;
    using Execute = std::function<void(const Data&, TechniqueExecuteContext&)>; */

    LambdaTechnique(Build build)
        : m_build(std::move(build)) {}


    void BuildGraph(TechniqueBuildContext& ctx) override {
        m_build(ctx);

    }

private:
    Build m_build;
}; 

} // namespace Lighthouse
