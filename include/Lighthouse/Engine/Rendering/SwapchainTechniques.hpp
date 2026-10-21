#pragma once

#include <functional>
#include <Lighthouse/Engine/Rendering/RenderView.hpp>

namespace Lighthouse {

class AcquireSwapchainImageTechnique final : public IRenderTechnique {
public:
    struct PassData { GraphImageHandle image{InvalidGraphImage}; };
    void BuildGraph(TechniqueBuildContext& ctx) override;
    const PassData& GetPassData() const noexcept { return m_passData; }
private:
    PassData m_passData;
};

class PresentSwapchainImageTechnique final : public IRenderTechnique {
public:
    struct PassData { GraphImageHandle image{InvalidGraphImage}; };
    template <typename Source>
    explicit PresentSwapchainImageTechnique(TechniqueRef<Source> source)
        : m_sourceImage([source](TechniqueBuildContext& ctx) {
            return ctx.GetTechnique(source).GetPassData().image;
        }) {}
    void BuildGraph(TechniqueBuildContext& ctx) override;
    const PassData& GetPassData() const noexcept { return m_passData; }
private:
    std::function<GraphImageHandle(TechniqueBuildContext&)> m_sourceImage;
    PassData m_passData;
};

} // namespace Lighthouse
