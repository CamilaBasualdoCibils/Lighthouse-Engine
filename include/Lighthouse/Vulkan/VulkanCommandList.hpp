#pragma once

#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>

#include <variant>
#include <vector>

namespace Lighthouse {

class VulkanCommandList final : public IRenderCommandList {
public:
    struct Begin { RenderPassDesc desc; };
    struct End {};
    struct BindPipelineCmd { GraphicsPipelineHandle handle; };
    struct BindDescriptorSetCmd { std::uint32_t setIndex; DescriptorSetHandle handle; };
    struct BindVertexCmd { BufferHandle handle; std::uint32_t stride; };
    struct BindIndexCmd { BufferHandle handle; };
    struct BindTextureCmd { std::uint32_t slot; ImageHandle handle; };
    struct DrawCmd { PrimitiveTopology topology; std::uint32_t count, first, instances, firstInstance; };
    struct DrawIndexedCmd { std::uint32_t count; };
    struct DispatchCmd { std::uint32_t x, y, z; };
    using Command = std::variant<Begin, End, BindPipelineCmd, BindDescriptorSetCmd, BindVertexCmd,
        BindIndexCmd, BindTextureCmd, DrawCmd, DrawIndexedCmd, DispatchCmd>;

    void BeginRendering(const RenderPassDesc& d) override { commands.emplace_back(Begin{d}); }
    void EndRendering() override { commands.emplace_back(End{}); }
    void BindPipeline(GraphicsPipelineHandle h) override { commands.emplace_back(BindPipelineCmd{h}); }
    void BindDescriptorSet(std::uint32_t setIndex, DescriptorSetHandle h) override {
        commands.emplace_back(BindDescriptorSetCmd{setIndex, h});
    }
    void BindVertexBuffer(BufferHandle h, std::uint32_t stride) override { commands.emplace_back(BindVertexCmd{h, stride}); }
    void BindIndexBuffer(BufferHandle h) override { commands.emplace_back(BindIndexCmd{h}); }
    void BindTexture(std::uint32_t slot, ImageHandle h) override { commands.emplace_back(BindTextureCmd{slot, h}); }
    void Draw(PrimitiveTopology t, std::uint32_t n, std::uint32_t first,
              std::uint32_t instances, std::uint32_t firstInstance) override {
        commands.emplace_back(DrawCmd{t, n, first, instances, firstInstance});
    }
    void DrawIndexed(std::uint32_t n) override { commands.emplace_back(DrawIndexedCmd{n}); }
    void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override { commands.emplace_back(DispatchCmd{x, y, z}); }

    const std::vector<Command>& Commands() const noexcept { return commands; }

private:
    std::vector<Command> commands;
};

} // namespace Lighthouse
