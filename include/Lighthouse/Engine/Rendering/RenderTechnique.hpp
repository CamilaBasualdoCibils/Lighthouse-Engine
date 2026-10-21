#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>
#include <stdexcept>
#include <type_traits>
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
                          RenderTechniqueHandle current,
                          const std::vector<RenderTechniqueHandle>* dependencies = nullptr,
                          const std::vector<RenderViewHandle>* viewDependencies = nullptr)
        : graph(graph), view(view), m_current(current),
          m_dependencies(dependencies), m_viewDependencies(viewDependencies) {}

    RenderGraph& graph;
    IRenderView& view;
    template <typename T> T* GetParentViewAs() noexcept;
    template <typename T> const T* GetParentViewAs() const noexcept;
    template <typename T> const T& GetTechnique(TechniqueRef<T> ref) const;
    void SetFinalImage(RenderGraphResource image) { m_finalImage = image; }
    const std::optional<RenderGraphResource>& FinalImage() const noexcept { return m_finalImage; }

    GraphImageHandle GetViewOutput(RenderViewHandle handle) const {
        if (!m_viewDependencies || std::find(m_viewDependencies->begin(), m_viewDependencies->end(), handle) == m_viewDependencies->end())
            throw std::logic_error("View output was not declared as a dependency");
        return graph.GetViewOutput(handle);
    }

private:
    RenderTechniqueHandle m_current;
    const std::vector<RenderTechniqueHandle>* m_dependencies{};
    const std::vector<RenderViewHandle>* m_viewDependencies{};
    std::optional<RenderGraphResource> m_finalImage;
};

class IRenderTechnique {
public:
    virtual ~IRenderTechnique() = default;
    virtual void BuildGraph(TechniqueBuildContext& context) = 0;
};

} // namespace Lighthouse
