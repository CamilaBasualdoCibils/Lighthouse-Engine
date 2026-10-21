#pragma once

#include <any>
#include <cstdint>
#include <optional>
#include <utility>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <vector>

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
    TechniqueBuildContext(RenderGraph& graph, IRenderView& view,
                          std::unordered_map<std::uint32_t, std::any>& outputs,
                          RenderTechniqueHandle current)
        : graph(graph), view(view), m_outputs(outputs), m_current(current) {}

    RenderGraph& graph;
    IRenderView& view;
    void SetFinalImage(RenderGraphResource image) { m_finalImage = image; }
    const std::optional<RenderGraphResource>& FinalImage() const noexcept { return m_finalImage; }

    template <typename T> const typename T::Outputs& GetOutputs(TechniqueRef<T> ref) const {
        if (ref.handle.owner != m_current.owner) throw std::logic_error("Technique belongs to another view");
        auto it = m_outputs.find(ref.handle.index);
        if (it == m_outputs.end()) throw std::logic_error("Technique output is missing");
        auto* value = std::any_cast<typename T::Outputs>(&it->second);
        if (!value) throw std::logic_error("Technique output type is incorrect");
        return *value;
    }

    template <typename T> void SetOutputs(T outputs) {
        m_outputs[m_current.index] = std::move(outputs);
    }

private:
    std::unordered_map<std::uint32_t, std::any>& m_outputs;
    RenderTechniqueHandle m_current;
    std::optional<RenderGraphResource> m_finalImage;
};

class IRenderTechnique {
public:
    virtual ~IRenderTechnique() = default;
    virtual void Build(TechniqueBuildContext& context) = 0;
};

} // namespace Lighthouse
