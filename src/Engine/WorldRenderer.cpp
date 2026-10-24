#include "Lighthouse/Engine/Scene/Entity.hpp"
#include <Lighthouse/Engine/Rendering/WorldRenderer.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <utility>

#include <Lighthouse/Engine/Scene/MeshRenderer.hpp>
#include <Lighthouse/Engine/Scene/Transform.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>

#include <algorithm>
#include <map>
#include <tuple>

namespace Lighthouse {
namespace {} // namespace

namespace {
struct BatchKey {
    const Material* material{};
    const Mesh* mesh{};
    std::uint32_t pipelineIndex{};
    std::uint32_t pipelineGeneration{};
    bool operator<(const BatchKey& rhs) const noexcept {
        return std::tie(material, mesh, pipelineIndex, pipelineGeneration) <
               std::tie(rhs.material, rhs.mesh, rhs.pipelineIndex, rhs.pipelineGeneration);
    }
};
}

struct StaticMeshRenderSystem::Batch {
    std::shared_ptr<Material> material;
    std::shared_ptr<Mesh> sourceMesh;
    RenderMesh mesh;
    std::vector<Mat4> transforms;
    GraphicsPipelineHandle pipeline{};
};

void WorldRenderer::AddSystem(std::unique_ptr<IWorldRenderSystem> system) {
    if (!system) throw std::invalid_argument("World render system cannot be null");
    m_systems.emplace_back(std::move(system));
}

void WorldRenderer::PrepareSystems(const RenderScene& scene) {
    for (auto& system : m_systems) system->Prepare(scene);
}

void WorldRenderer::DrawSystems(IRenderCommandList& commands, WorldRenderPass pass,
                                WorldDrawContext& context) {
    for (auto& system : m_systems) system->Draw(commands, pass, context);
}

void StaticMeshRenderSystem::Prepare(const RenderScene& scene) {
    m_batches.clear();
    std::map<BatchKey, Batch*> batches;
    for (const auto& [pipeline, instance] : scene.meshes) {
        if (!instance.mesh) continue;
        auto mesh = instance.mesh->ToRenderMesh();
        const auto key = BatchKey{instance.material.get(), instance.mesh.get(), pipeline.index, pipeline.generation};
        auto found = batches.find(key);
        if (found == batches.end()) {
            auto batch = std::make_unique<Batch>();
            batch->material = instance.material;
            batch->sourceMesh = instance.mesh;
            batch->mesh = std::move(mesh);
            batch->pipeline = pipeline;
            auto* ptr = batch.get();
            m_batches.emplace_back(std::move(batch));
            found = batches.emplace(key, ptr).first;
        }
        found->second->transforms.push_back(instance.transform);
    }
}

void StaticMeshRenderSystem::Draw(IRenderCommandList& commands, WorldRenderPass pass,
                                  WorldDrawContext&) {
    // No GBuffer pipeline variant is available in Material yet. Keep the pass
    // unsupported and safe until the material/device APIs can provide one.
    if (pass != WorldRenderPass::Forward) return;
    // The current renderer has no upload path for CPU mesh data and no indexed
    // command carrying index format/offset. Draw only resident, non-indexed meshes.
    for (const auto& batch : m_batches) {
        const auto& mesh = batch->mesh;
        if (!batch->pipeline || mesh.vertexBuffers.empty() || mesh.indexCount != 0) continue;
        commands.BindPipeline(batch->pipeline);
        for (const auto& binding : mesh.vertexBuffers) {
            auto layout = std::find_if(mesh.layout.bindings.begin(), mesh.layout.bindings.end(),
                [&](const VertexBinding& candidate) { return candidate.binding == binding.binding; });
            if (layout == mesh.layout.bindings.end()) continue;
            commands.BindVertexBuffer(binding.buffer, layout->stride);
        }
        commands.Draw(mesh.topology, mesh.vertexCount, 0,
                      static_cast<std::uint32_t>(batch->transforms.size()), 0);
    }
}

void ForwardWorldRenderer::BuildGraph(RenderGraph& graph, const RenderScene& scene,
                                      RenderGraphResource output) {
    PrepareSystems(scene);
    struct PassData { RenderGraphResource output{InvalidGraphImage}; };
    graph.AddPass<PassData>("Forward World Geometry",
        [output](RenderGraph::Builder& builder, PassData& data) {
            data.output = builder.Write(output);
        },
        [this](const PassData& data, FrameGraphPassResources& resources, IRenderDevice& device) {
            const auto& target = resources.get<ImportedRenderGraphImage>(data.output);
            const auto& desc = resources.getDescriptor<ImportedRenderGraphImage>(data.output);
            auto commands = device.CreateCommandList(QueueType::Graphics);
            if (!commands) return;
            RenderPassDesc renderPass;
            renderPass.colorAttachment = target.view;
            renderPass.width = desc.width;
            renderPass.height = desc.height;
            commands->BeginRendering(renderPass);
            WorldDrawContext context{device};
            DrawSystems(*commands, WorldRenderPass::Forward, context);
            commands->EndRendering();
            device.Submit(*commands);
        });
}

void DeferredWorldRenderer::BuildGraph(RenderGraph& graph, const RenderScene& scene,
                                       RenderGraphResource output) {
    PrepareSystems(scene);
    // Current RenderPassDesc supports one color attachment, so a true shared
    // multi-target GBuffer is deferred until the device abstraction adds MRT.
    // Keep the graph stages and dependency explicit for that integration.
    struct GeometryData { RenderGraphResource output{InvalidGraphImage}; };
    const auto& geometry = graph.AddPass<GeometryData>("Deferred GBuffer Geometry",
        [output](RenderGraph::Builder& builder, GeometryData& data) {
            data.output = builder.Write(output);
        },
        [this](const GeometryData& data, FrameGraphPassResources& resources, IRenderDevice& device) {
            const auto& target = resources.get<ImportedRenderGraphImage>(data.output);
            const auto& desc = resources.getDescriptor<ImportedRenderGraphImage>(data.output);
            auto commands = device.CreateCommandList(QueueType::Graphics);
            if (!commands) return;
            RenderPassDesc renderPass;
            renderPass.colorAttachment = target.view;
            renderPass.width = desc.width;
            renderPass.height = desc.height;
            commands->BeginRendering(renderPass);
            WorldDrawContext context{device};
            DrawSystems(*commands, WorldRenderPass::GBuffer, context);
            commands->EndRendering();
            device.Submit(*commands);
        });
    graph.AddPass<GeometryData>("Deferred Lighting Composite",
        [&geometry](RenderGraph::Builder& builder, GeometryData& data) {
            data.output = builder.Read(geometry.output);
            data.output = builder.Write(data.output);
        },
        [](const GeometryData&, FrameGraphPassResources&, IRenderDevice&) {});
}

void WorldRenderer::Extract(const Scene &scene, RenderScene &output) const {
  output.Clear();
  if (!scene.m_registry)
    return;

  auto view = scene.m_registry->view<const MeshRenderer>();
  for (const auto entity : view) {
    const auto &renderer = view.get<const MeshRenderer>(entity);
    if (!renderer.mesh)
      continue;

    MeshInstance instance;
    instance.mesh = renderer.mesh;
    instance.material = renderer.material;
    if (scene.m_registry->all_of<Transform>(entity)) {
      const Transform &transform =
          scene.m_registry->get<const Transform>(entity);
      instance.transform = transform.GetMatrix();

      std::optional<EntityID> parent = transform.GetParent();
      while (parent.has_value()) {
        const Transform &parentTransform =
            scene.m_registry->get<const Transform>(*parent);
        instance.transform = parentTransform.GetMatrix() * instance.transform;
        parent = parentTransform.GetParent();
      }
    }

    // MeshRenderer does not yet assign a graphics pipeline, so extracted
    // instances use the invalid/default pipeline key until that is defined.
    output.meshes.emplace(GraphicsPipelineHandle{}, std::move(instance));
  }
}

} // namespace Lighthouse
