#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include <Lighthouse/Engine/Rendering/RenderScene.hpp>
#include <Lighthouse/Engine/Rendering/RenderTechnique.hpp>
#include <Lighthouse/Engine/Scene/Scene.hpp>

namespace Lighthouse {

enum class WorldRenderPass { Forward, GBuffer, DepthOnly, Shadow };

struct WorldDrawContext {
    IRenderDevice& device;
};

class IWorldRenderSystem {
public:
    virtual ~IWorldRenderSystem() = default;
    virtual void Prepare(const RenderScene& scene) = 0;
    // Unsupported passes are a no-op.
    virtual void Draw(IRenderCommandList& commands, WorldRenderPass pass,
                      WorldDrawContext& context) = 0;
};

class WorldRenderer {
public:
    virtual ~WorldRenderer() = default;
    void Extract(const Scene& scene, RenderScene& output) const;

    template <typename T, typename... Args> T& AddSystem(Args&&... args) {
        static_assert(std::is_base_of_v<IWorldRenderSystem, T>);
        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        auto& result = *system;
        m_systems.emplace_back(std::move(system));
        return result;
    }
    void AddSystem(std::unique_ptr<IWorldRenderSystem> system);

    virtual void BuildGraph(RenderGraph& graph, const RenderScene& scene,
                            RenderGraphResource output) {
        (void)graph; (void)scene; (void)output;
    }

protected:
    void PrepareSystems(const RenderScene& scene);
    void DrawSystems(IRenderCommandList& commands, WorldRenderPass pass,
                     WorldDrawContext& context);
    std::vector<std::unique_ptr<IWorldRenderSystem>> m_systems;
};

class StaticMeshRenderSystem final : public IWorldRenderSystem {
public:
    void Prepare(const RenderScene& scene) override;
    void Draw(IRenderCommandList& commands, WorldRenderPass pass,
              WorldDrawContext& context) override;

private:
    struct Batch;
    std::vector<std::unique_ptr<Batch>> m_batches;
};

class ForwardWorldRenderer final : public WorldRenderer {
public:
    void BuildGraph(RenderGraph& graph, const RenderScene& scene,
                    RenderGraphResource output) override;
};

class DeferredWorldRenderer final : public WorldRenderer {
public:
    void BuildGraph(RenderGraph& graph, const RenderScene& scene,
                    RenderGraphResource output) override;
};

// Adapter for attaching a renderer pipeline to an existing render view.
class WorldRenderTechnique final : public IRenderTechnique {
public:
    WorldRenderTechnique(WorldRenderer& renderer, RenderScene scene,
                         RenderGraphResource output)
        : m_renderer(renderer), m_scene(std::move(scene)), m_output(output) {}
    void BuildGraph(TechniqueBuildContext& context) override {
        m_renderer.BuildGraph(context.graph, m_scene, m_output);
    }
private:
    WorldRenderer& m_renderer;
    RenderScene m_scene;
    RenderGraphResource m_output;
};

} // namespace Lighthouse
