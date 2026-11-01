#pragma once

#include "Lighthouse/Engine/Rendering/RenderGraph.hpp"
#include "Lighthouse/Engine/Rendering/RenderTechnique.hpp"
#include "Lighthouse/Engine/Rendering/RenderTypes.hpp"
namespace Lighthouse {

struct WorldRenderGbufferPassData {
  RenderGraphResource albedo;
  RenderGraphResource normal;
  RenderGraphResource depth;
  RenderGraphResource roughness;
  RenderGraphResource metallic;
};
struct WorldRenderPassData {
  RenderGraphResource color;
};
class WorldRenderTechnique : public IRenderTechnique {

public:
  WorldRenderTechnique() = default;
  void BuildGraph(TechniqueBuildContext &context) override {
    m_gbufferPassData = context.graph.AddPass<WorldRenderGbufferPassData>(
        "WorldRenderGbuffer",
        [](Lighthouse::RenderGraph::Builder &builder,
           WorldRenderGbufferPassData &data) {
          data.albedo = builder.Create<RenderGraphTexture>(
              std::string_view("Albedo"),
              RenderGraphTexture::Desc{.format = ImageFormat::RGBA8});
          data.albedo = builder.Write(data.albedo);
          data.normal = builder.Create<RenderGraphTexture>(
              std::string_view("Normal"),
              RenderGraphTexture::Desc{.format = ImageFormat::RGBA8});
          data.normal = builder.Write(data.normal);
          data.depth = builder.Create<RenderGraphTexture>(
              std::string_view("Depth"),
              RenderGraphTexture::Desc{.format = ImageFormat::Depth32});
          data.depth = builder.Write(data.depth);
          data.roughness = builder.Create<RenderGraphTexture>(
              std::string_view("Roughness"),
              RenderGraphTexture::Desc{.format = ImageFormat::R8});
          data.roughness = builder.Write(data.roughness);
          data.metallic = builder.Create<RenderGraphTexture>(
              std::string_view("Metallic"),
              RenderGraphTexture::Desc{.format = ImageFormat::R8});
          data.metallic = builder.Write(data.metallic);
        },
        [](const WorldRenderGbufferPassData &data,
           FrameGraphPassResources &resources,IRenderCommandList& context) {

        });
    m_worldRenderPassData =context.graph.AddPass<WorldRenderPassData>(
        "WorldRender",
        [&](Lighthouse::RenderGraph::Builder &builder,
           WorldRenderPassData &data) { 
              builder.Read(m_gbufferPassData.albedo);
              builder.Read(m_gbufferPassData.normal);
              builder.Read(m_gbufferPassData.depth);
              builder.Read(m_gbufferPassData.roughness);
              builder.Read(m_gbufferPassData.metallic);
              data.color = builder.Create<RenderGraphTexture>(
                  std::string_view("Color"),
                  RenderGraphTexture::Desc{.format = ImageFormat::RGBA8});
              data.color = builder.Write(data.color);
           },
        [](const WorldRenderPassData &data,
           FrameGraphPassResources &resources,IRenderCommandList& context) {

        });
  }
  const WorldRenderGbufferPassData& GetWorldRenderGbufferPassData() const { return m_gbufferPassData; }
  const WorldRenderPassData& GetWorldRenderPassData() const { return m_worldRenderPassData; }
private:
  WorldRenderGbufferPassData m_gbufferPassData;
  WorldRenderPassData m_worldRenderPassData;
};
} // namespace Lighthouse