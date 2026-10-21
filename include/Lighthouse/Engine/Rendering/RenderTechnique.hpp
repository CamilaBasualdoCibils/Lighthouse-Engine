#pragma once

#include <any>
#include <algorithm>
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
class WindowManager;

template <typename T> struct TechniqueRef {
    RenderTechniqueHandle handle;
    explicit operator bool() const noexcept { return static_cast<bool>(handle); }
};

class TechniqueBuildContext {
public:
    TechniqueBuildContext(RenderGraph& graph, IRenderView& view,
                          std::unordered_map<std::uint32_t, std::any>& outputs,
                          RenderTechniqueHandle current,
                          const std::vector<RenderTechniqueHandle>* dependencies = nullptr,
                          const std::vector<RenderViewHandle>* viewDependencies = nullptr,
                          const std::vector<std::pair<RenderViewHandle, GraphImageHandle>>* viewOutputs = nullptr,
                          WindowManager* windows = nullptr)
        : graph(graph), view(view), m_outputs(outputs), m_current(current),
          m_dependencies(dependencies), m_viewDependencies(viewDependencies), m_viewOutputs(viewOutputs),
          windows(windows) {}

    RenderGraph& graph;
    IRenderView& view;
    WindowManager* windows{};
    void SetFinalImage(RenderGraphResource image) { m_finalImage = image; }
    const std::optional<RenderGraphResource>& FinalImage() const noexcept { return m_finalImage; }

    GraphImageHandle GetViewOutput(RenderViewHandle handle) const {
        if (!m_viewDependencies || std::find(m_viewDependencies->begin(), m_viewDependencies->end(), handle) == m_viewDependencies->end())
            throw std::logic_error("View output was not declared as a dependency");
        if (m_viewOutputs) for (const auto& [viewHandle, output] : *m_viewOutputs)
            if (viewHandle == handle) return output;
        throw std::logic_error("View output is missing");
    }

    template <typename T> const typename T::Outputs& GetOutputs(TechniqueRef<T> ref) const {
        if (m_dependencies && std::find(m_dependencies->begin(), m_dependencies->end(), ref.handle) == m_dependencies->end())
            throw std::logic_error("Technique output was not declared as a dependency");
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
    const std::vector<RenderTechniqueHandle>* m_dependencies{};
    const std::vector<RenderViewHandle>* m_viewDependencies{};
    const std::vector<std::pair<RenderViewHandle, GraphImageHandle>>* m_viewOutputs{};
    std::optional<RenderGraphResource> m_finalImage;
};

class IRenderTechnique {
public:
    virtual ~IRenderTechnique() = default;
    virtual void BuildGraph(TechniqueBuildContext& context) = 0;
};

} // namespace Lighthouse
