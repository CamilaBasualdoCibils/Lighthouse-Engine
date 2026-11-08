#include <Lighthouse/Engine/Rendering/Graph/RenderGraph.hpp>
#include <gtest/gtest.h>
using namespace Lighthouse;
namespace
{
struct Resource
{
  struct Desc
  {
  };
  void create(const Desc&, void*) {}
  void destroy(const Desc&, void*) {}
};
const auto execute = [](const FrameGraph::NoData&, FrameGraphPassResources&,
                        RenderGraphExecutePassContext&) {};
} // namespace
TEST(RenderGraphInspection, RecordsNamesVersionsAndAccesses)
{
  RenderGraph graph;
  auto imported = graph.Import<Resource>("External", {}, {});
  RenderGraphResource first{}, second{};
  graph.AddPass(
      "Produce",
      [&](RenderGraph::Builder& builder, FrameGraph::NoData&)
      {
        first = builder.Create<Resource>("Color", {});
        EXPECT_EQ(builder.Write(first), first);
        builder.Read(imported);
        builder.Read(imported);
      },
      execute);
  graph.AddPass(
      "Modify",
      [&](RenderGraph::Builder& builder, FrameGraph::NoData&)
      {
        builder.Read(first);
        second = builder.Write(first);
      },
      execute);
  graph.AddPass(
      "Consume",
      [&](RenderGraph::Builder& builder, FrameGraph::NoData&)
      {
        builder.Read(second);
        builder.SetSideEffect();
      },
      execute);
  const auto data = graph.GetInspectionData();
  ASSERT_EQ(data.passes.size(), 3u);
  ASSERT_EQ(data.resources.size(), 3u);
  EXPECT_EQ(data.passes[0].name, "Produce");
  EXPECT_EQ(data.passes[0].creates, std::vector<RenderGraphResource>{first});
  EXPECT_EQ(data.passes[0].writes, std::vector<RenderGraphResource>{first});
  EXPECT_EQ(data.passes[0].reads, std::vector<RenderGraphResource>{imported});
  EXPECT_EQ(data.passes[1].reads, std::vector<RenderGraphResource>{first});
  EXPECT_EQ(data.passes[1].writes, std::vector<RenderGraphResource>{second});
  EXPECT_EQ(data.passes[2].reads, std::vector<RenderGraphResource>{second});
  EXPECT_EQ(data.resources[0].name, "External");
  EXPECT_TRUE(data.resources[0].imported);
  EXPECT_EQ(data.resources[1].name, "Color");
  EXPECT_FALSE(data.resources[1].imported);
  EXPECT_EQ(data.resources[1].version, 1u);
  EXPECT_EQ(data.resources[2].version, 2u);
  EXPECT_EQ(data.resources[2].resource, first);
  EXPECT_EQ(data.resources[2].handle, second);
  EXPECT_EQ(graph.GetResourceNodes(),
            (std::vector<RenderGraphResource>{imported, second}));
  graph.Compile();
  EXPECT_EQ(graph.GetInspectionData().resources.size(), data.resources.size());
}
TEST(RenderGraphInspection, RecordsImplicitWriteDependencies)
{
  RenderGraph graph;
  const auto initial = graph.Import<Resource>("Image", {}, {});
  RenderGraphResource next{};
  graph.AddPass(
      "Write", [&](RenderGraph::Builder& builder, FrameGraph::NoData&)
      { next = builder.Write(initial); }, execute);
  const auto data = graph.GetInspectionData();
  ASSERT_EQ(data.passes.size(), 1u);
  EXPECT_EQ(data.passes[0].reads, std::vector<RenderGraphResource>{initial});
  EXPECT_EQ(data.passes[0].writes, std::vector<RenderGraphResource>{next});
  EXPECT_EQ(data.resources[1].resource, initial);
  EXPECT_TRUE(data.resources[1].imported);
}
TEST(RenderGraphInspection, ExplicitScopesHideUiRegardlessOfNamesAndRestore)
{
  RenderGraph graph;
  auto visible = graph.Import<Resource>("ImGui is only a name", {}, {});
  RenderGraphResource hidden{};
  {
    auto scope = graph.HideFromInspector();
    hidden = graph.Import<Resource>("Private", {}, {});
    graph.AddPass(
        "Not a name prefix",
        [&](RenderGraph::Builder& builder, FrameGraph::NoData&)
        {
          builder.Read(visible);
          hidden = builder.Write(hidden);
        },
        execute);
    {
      auto nested = graph.HideFromInspector();
      (void)graph.Import<Resource>("Nested", {}, {});
    }
    (void)graph.Import<Resource>("Still hidden", {}, {});
  }
  graph.AddPass(
      "Visible pass",
      [&](RenderGraph::Builder& builder, FrameGraph::NoData&)
      {
        builder.Read(visible);
        builder.Read(hidden);
      },
      execute);
  auto filtered = graph.GetInspectionData();
  ASSERT_EQ(filtered.passes.size(), 1u);
  ASSERT_EQ(filtered.resources.size(), 1u);
  EXPECT_EQ(filtered.passes[0].name, "Visible pass");
  EXPECT_EQ(filtered.passes[0].reads,
            std::vector<RenderGraphResource>{visible});
  EXPECT_EQ(filtered.resources[0].name, "ImGui is only a name");
  const auto all = graph.GetInspectionData(true);
  EXPECT_EQ(all.passes.size(), 2u);
  EXPECT_EQ(all.resources.size(), 5u);
  EXPECT_TRUE(all.passes[0].hidden);
  EXPECT_FALSE(all.passes[1].hidden);
  EXPECT_FALSE(all.resources[0].hidden);
  EXPECT_TRUE(all.resources[2].hidden);
  EXPECT_NO_THROW(
      graph.Compile()); // Filtering never removes actual passes/resources.
}
TEST(RenderGraphInspection, SnapshotsMovesClearAndScopeExceptions)
{
  RenderGraph graph;
  try
  {
    auto scope = graph.HideFromInspector();
    throw std::runtime_error("Test");
  }
  catch (const std::runtime_error&)
  {
  }
  (void)graph.Import<Resource>("Visible", {}, {});
  const auto snapshot = graph.GetInspectionData();
  (void)graph.Import<Resource>("Later", {}, {});
  EXPECT_EQ(snapshot.resources.size(), 1u);
  RenderGraph moved(std::move(graph));
  ASSERT_EQ(moved.GetInspectionData().resources.size(), 2u);
  EXPECT_EQ(moved.GetInspectionData().resources[0].name, "Visible");
  RenderGraph assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.GetInspectionData().resources.size(), 2u);
  assigned.Clear();
  EXPECT_TRUE(assigned.GetInspectionData(true).resources.empty());
  EXPECT_TRUE(assigned.GetInspectionData(true).passes.empty());
  EXPECT_TRUE(assigned.GetResourceNodes().empty());
  (void)assigned.Import<Resource>("Fresh", {}, {});
  EXPECT_EQ(assigned.GetInspectionData().resources[0].version, 1u);
}
