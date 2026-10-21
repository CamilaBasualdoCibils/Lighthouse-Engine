#pragma once

#include <memory>

#include <Lighthouse/Engine/Handle.hpp>

namespace Engine {

class RenderGraph;
class RenderScene;
class RenderView;

class IRenderTechnique {
public:
    virtual ~IRenderTechnique() = default;
    virtual void Build(RenderGraph& graph, const RenderView& view, const RenderScene& scene) = 0;
};

} // namespace Engine
