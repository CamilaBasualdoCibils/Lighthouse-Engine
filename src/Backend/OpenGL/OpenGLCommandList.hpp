#pragma once

#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>
#include <stdexcept>

#include <variant>
#include <vector>

namespace Lighthouse {

class OpenGLRenderDevice;

class OpenGLCommandList final : public IRenderCommandList {
public:
  void Begin() override {
    if (Begun) throw std::logic_error("Command list has already begun");
    Begun = true;
  }

  void End() override {
    if (!Begun) throw std::logic_error("Command list has not begun");
    Begun = false;
  }

  void Clear() override {
    m_commands.clear();
  }

    struct BeginRenderCommand {
        RenderPassDesc desc;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct EndRenderCommand {
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct BindPipelineCommand {
        GraphicsPipelineHandle handle;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct BindDescriptorSetCommand {
        std::uint32_t setIndex;
        DescriptorSetHandle handle;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct BindVertexBufferCommand {
        BufferHandle handle;
        std::uint32_t stride;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct BindIndexBufferCommand {
        BufferHandle handle;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct BindTextureCommand {
        std::uint32_t slot;
        ImageHandle handle;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct DrawCommand {
        PrimitiveTopology topology;
        std::uint32_t count, first, instances, firstInstance;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct DrawIndexedCommand {
        std::uint32_t count;
        std::uint32_t firstIndex{};
        std::int32_t baseVertex{};
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct DispatchCommand {
        std::uint32_t x, y, z;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct BlitImageCommand {
        ImageHandle src;
        ImageHandle dst;
        BlitRegion region;
        ImageFilter filter;
        void Execute(OpenGLRenderDevice *device) const;
    };
    using Command = std::variant<BeginRenderCommand, EndRenderCommand, BindPipelineCommand,
        BindDescriptorSetCommand,
        BindVertexBufferCommand, BindIndexBufferCommand, BindTextureCommand,
        DrawCommand, DrawIndexedCommand, DispatchCommand, BlitImageCommand>;

    const std::vector<Command>& Commands() const noexcept { return m_commands; }
    void BeginRendering(const RenderPassDesc& desc) override { m_commands.emplace_back(BeginRenderCommand{desc}); }
    void EndRendering() override { m_commands.emplace_back(EndRenderCommand{}); }
    void BindPipeline(GraphicsPipelineHandle handle) override { m_commands.emplace_back(BindPipelineCommand{handle}); }
    void BindDescriptorSet(std::uint32_t setIndex, DescriptorSetHandle handle) override {
        m_commands.emplace_back(BindDescriptorSetCommand{setIndex, handle});
    }
    void BindVertexBuffer(BufferHandle handle, std::uint32_t stride) override { m_commands.emplace_back(BindVertexBufferCommand{handle, stride}); }
    void BindIndexBuffer(BufferHandle handle) override { m_commands.emplace_back(BindIndexBufferCommand{handle}); }
    void BindTexture(std::uint32_t slot, ImageHandle handle) override { m_commands.emplace_back(BindTextureCommand{slot, handle}); }
    void Draw(PrimitiveTopology topology, std::uint32_t count, std::uint32_t first,
              std::uint32_t instances, std::uint32_t firstInstance) override {
        m_commands.emplace_back(DrawCommand{topology, count, first, instances, firstInstance});
    }
    void DrawIndexed(std::uint32_t count) override { m_commands.emplace_back(DrawIndexedCommand{count}); }
    void DrawIndexedRange(std::uint32_t count, std::uint32_t firstIndex,
                          std::int32_t baseVertex) override {
        m_commands.emplace_back(DrawIndexedCommand{count, firstIndex, baseVertex});
    }
    void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override { m_commands.emplace_back(DispatchCommand{x, y, z}); }
  void BlitImage(ImageHandle src, ImageHandle dst, const BlitRegion &region,
                 ImageFilter filter) override {
      m_commands.emplace_back(BlitImageCommand{src, dst, region, filter});
  }
private:
    std::vector<Command> m_commands;
    bool Begun = false;
};

} // namespace Lighthouse
