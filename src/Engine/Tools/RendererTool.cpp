#ifdef LIGHTHOUSE_IMGUI
#include <Lighthouse/Engine/Tools/RendererTool.hpp>
#include <algorithm>
#include <cmath>
#include <deque>
#include <imgui-node-editor/imgui_node_editor.h>
#include <numeric>
#include <set>
#include <unordered_map>
namespace Lighthouse
{
struct RendererTool::Diagram
{
  struct Node
  {
    std::uintptr_t id;
    std::string name;
    bool pass;
    RenderGraphResource handle;
    std::uint32_t version;
    bool imported;
    std::size_t layer{};
    ImVec2 position{}, size{260, 110};
  };
  std::vector<Node> nodes;
  std::vector<std::pair<std::size_t, std::size_t>> edges;
  std::vector<std::vector<std::size_t>> layers;
  std::size_t passCount{}, resourceCount{}, signature{};
  std::uintptr_t linkBase{1};
  void Layout()
  {
    float x = 0;
    for (const auto& layer : layers)
    {
      float y = 0, width = 0;
      for (auto index : layer)
      {
        auto& node = nodes[index];
        node.position = {x, y};
        y += node.size.y + 40;
        width = std::max(width, node.size.x);
      }
      x += width + 100;
    }
  }
};
std::shared_ptr<RendererTool::Diagram>
RendererTool::MakeDiagram(RenderGraph::InspectionData data)
{
  auto diagram = std::make_shared<Diagram>();
  diagram->passCount = data.passes.size();
  diagram->resourceCount = data.resources.size();
  std::unordered_map<RenderGraphResource, std::size_t> resources;
  for (const auto& resource : data.resources)
  {
    resources.emplace(resource.handle, diagram->nodes.size());
    diagram->nodes.push_back({(std::uintptr_t(resource.handle) + 1) * 8,
                              resource.name, false, resource.handle,
                              resource.version, resource.imported});
  }
  std::set<std::pair<std::size_t, std::size_t>> edges;
  for (const auto& pass : data.passes)
  {
    const auto index = diagram->nodes.size();
    diagram->nodes.push_back({(std::uintptr_t(pass.id) * 2 + 1) * 4, pass.name,
                              true, kInvalidGraphResource, 0, false});
    for (auto handle : pass.reads)
      if (const auto found = resources.find(handle); found != resources.end())
        edges.emplace(found->second, index);
    for (const auto* outputs : {&pass.creates, &pass.writes})
      for (auto handle : *outputs)
        if (const auto found = resources.find(handle); found != resources.end())
          edges.emplace(index, found->second);
  }
  diagram->edges.assign(edges.begin(), edges.end());
  std::vector<std::vector<std::size_t>> children(diagram->nodes.size()),
      parents(diagram->nodes.size());
  std::vector<std::size_t> incoming(diagram->nodes.size());
  for (auto [from, to] : diagram->edges)
  {
    children[from].push_back(to);
    parents[to].push_back(from);
    ++incoming[to];
  }
  std::deque<std::size_t> ready;
  for (std::size_t i = 0; i < incoming.size(); ++i)
    if (!incoming[i])
      ready.push_back(i);
  while (!ready.empty())
  {
    const auto from = ready.front();
    ready.pop_front();
    for (auto to : children[from])
    {
      diagram->nodes[to].layer =
          std::max(diagram->nodes[to].layer, diagram->nodes[from].layer + 1);
      if (!--incoming[to])
        ready.push_back(to);
    }
  }
  for (std::size_t i = 0; i < diagram->nodes.size(); ++i)
  {
    const auto layer = diagram->nodes[i].layer;
    if (diagram->layers.size() <= layer)
      diagram->layers.resize(layer + 1);
    diagram->layers[layer].push_back(i);
  }
  // Order each layer by its parents' rows to reduce crossings
  // deterministically.
  std::vector<float> rows(diagram->nodes.size());
  for (auto& layer : diagram->layers)
  {
    auto center = [&](auto node)
    {
      float sum = 0;
      for (auto parent : parents[node])
        sum += rows[parent];
      return parents[node].empty() ? float(node)
                                   : sum / float(parents[node].size());
    };
    std::stable_sort(layer.begin(), layer.end(),
                     [&](auto a, auto b) { return center(a) < center(b); });
    for (std::size_t row = 0; row < layer.size(); ++row)
      rows[layer[row]] = float(row);
  }
  auto hash = [&](std::size_t value)
  {
    diagram->signature ^= value + std::size_t(0x9e3779b9) +
                          (diagram->signature << 6) + (diagram->signature >> 2);
  };
  for (const auto& node : diagram->nodes)
  {
    hash(node.id);
    hash(std::hash<std::string>{}(node.name));
    hash(node.version);
    hash(node.imported);
    diagram->linkBase = std::max(diagram->linkBase, node.id + 4);
  }
  for (auto [from, to] : diagram->edges)
  {
    hash(from);
    hash(to);
  }
  diagram->Layout();
  return diagram;
}
RendererTool::~RendererTool()
{
  if (m_editor)
    ax::NodeEditor::DestroyEditor(m_editor);
}
RenderGraphResource RendererTool::BuildGraph(ImguiBuildContext& context,
                                             RenderGraphResource completionFlag)
{
  struct Data
  {
    RenderGraphResource completion;
    std::shared_ptr<Diagram> diagram;
  };
  auto diagram = MakeDiagram(context.graph.GetInspectionData());
  return context
      .AddPass<Data>(
          Name(),
          [completionFlag, diagram](RenderGraph::Builder& builder, Data& data)
          {
            data.diagram = diagram;
            data.completion = builder.Write(completionFlag);
          },
          [this, dockspace = context.dockspace](const Data& data,
                                                FrameGraphPassResources&,
                                                RenderGraphExecutePassContext&)
          {
            if (dockspace)
              ImGui::SetNextWindowDockID(dockspace, ImGuiCond_FirstUseEver);
            const bool visible = ImGui::Begin("Renderer");
            try
            {
              if (visible)
                Draw(data.diagram);
            }
            catch (...)
            {
              ImGui::End();
              throw;
            }
            ImGui::End();
          })
      .completion;
}
void RendererTool::Draw(const std::shared_ptr<Diagram>& diagram)
{
  namespace ed = ax::NodeEditor;
  auto* previous = ed::GetCurrentEditor();
  const bool changed = !m_hasSignature || m_signature != diagram->signature;
  if (changed && m_editor)
  {
    if (previous == m_editor)
      previous = nullptr;
    ed::DestroyEditor(m_editor);
    m_editor = nullptr;
  }
  if (!m_editor)
  {
    ed::Config config;
    config.SettingsFile = nullptr;
    m_editor = ed::CreateEditor(&config);
  }
  struct Restore
  {
    ed::EditorContext* previous;
    ~Restore()
    {
      ed::SetCurrentEditor(previous);
    }
  } restore{previous};
  ed::SetCurrentEditor(m_editor);
  ed::EnableShortcuts(false);
  if (changed)
  {
    m_signature = diagram->signature;
    m_hasSignature = true;
    m_fitFrames = 2;
  }
  ImGui::Text("%zu passes / %zu resource versions (read only)",
              diagram->passCount, diagram->resourceCount);
  if (ImGui::Button("Fit graph"))
    m_fitFrames = 2;
  ImGui::SameLine();
  ImGui::TextDisabled("Pan / zoom / select");
  if (diagram->nodes.empty())
    ImGui::TextUnformatted("No rendering passes or resources.");
  ed::Begin("RendererGraph");
  for (const auto& node : diagram->nodes)
  {
    ed::SetNodePosition(ed::NodeId(node.id), node.position);
    ed::PushStyleColor(ed::StyleColor_NodeBg,
                       node.pass ? ImVec4(.10f, .19f, .29f, 1)
                                 : ImVec4(.24f, .19f, .09f, 1));
    ed::BeginNode(ed::NodeId(node.id));
    if (node.pass)
      ImGui::TextUnformatted("Pass");
    else
      ImGui::Text("Resource v%u%s", node.version,
                  node.imported ? " (imported)" : "");
    ImGui::Separator();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 240);
    ImGui::TextUnformatted(node.name.c_str());
    ImGui::PopTextWrapPos();
    if (!node.pass)
      ImGui::TextDisabled("Handle %d", node.handle);
    ImGui::Dummy(ImVec2(240, 0));
    ed::BeginPin(ed::PinId(node.id + 1), ed::PinKind::Input);
    ImGui::TextUnformatted("in");
    ed::EndPin();
    ImGui::SameLine(0, 180);
    ed::BeginPin(ed::PinId(node.id + 2), ed::PinKind::Output);
    ImGui::TextUnformatted("out");
    ed::EndPin();
    ed::EndNode();
    ed::PopStyleColor();
  }
  for (std::size_t i = 0; i < diagram->edges.size(); ++i)
  {
    const auto [from, to] = diagram->edges[i];
    ed::Link(ed::LinkId(diagram->linkBase + i),
             ed::PinId(diagram->nodes[from].id + 2),
             ed::PinId(diagram->nodes[to].id + 1),
             diagram->nodes[from].pass ? ImVec4(1, .7f, .3f, 1)
                                       : ImVec4(.4f, .8f, 1, 1));
  }
  // No create/delete/shortcut handlers: the source graph is never editable.
  ed::End();
  bool resized = false;
  for (auto& node : diagram->nodes)
  {
    const auto size = ed::GetNodeSize(ed::NodeId(node.id));
    if (size.x > 0 && size.y > 0 &&
        (std::abs(size.x - node.size.x) > .5f ||
         std::abs(size.y - node.size.y) > .5f))
    {
      node.size = size;
      resized = true;
    }
  }
  if (resized)
    diagram->Layout();
  // Keep automatic positions fixed while retaining canvas navigation/selection.
  for (const auto& node : diagram->nodes)
    ed::SetNodePosition(ed::NodeId(node.id), node.position);
  if (m_fitFrames > 0 && !diagram->nodes.empty())
  {
    ed::NavigateToContent(0);
    --m_fitFrames;
  }
}
} // namespace Lighthouse
#endif
