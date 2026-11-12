#pragma once

#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>
#include <stdexcept>

#include <variant>
#include <vector>

namespace Lighthouse {

class OpenGLRenderDevice;
namespace Detail
{
std::uint32_t OpenGLMemoryBarrierBits(const MemoryBarrierDesc&);
}

class OpenGLCommandList final : public IRenderCommandList {
public:
  explicit OpenGLCommandList(std::uint64_t owner) : m_owner(owner) {}
  std::uint64_t Owner() const noexcept
  {
    return m_owner;
  }
  bool IsRecording() const noexcept
  {
    return Begun;
  }
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
    m_rendering = false;
  }

    struct BeginRenderCommand {
        RenderPassDesc desc;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct StencilCommand {
        StencilMode mode;
        std::uint32_t reference;
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
    struct CopyBufferCommand
    {
      BufferHandle src, dst;
      std::uint64_t srcOffset, dstOffset, bytes;
      void Execute(OpenGLRenderDevice*) const;
    };
    struct DispatchCommand {
        std::uint32_t x, y, z;
        void Execute(OpenGLRenderDevice* device) const;
    };
    struct GenMipMapsCommand {
        GenMipMapsDesc desc;
        void Execute(OpenGLRenderDevice*) const;
    };
    struct BlitImageCommand {
        ImageHandle src;
        ImageHandle dst;
        BlitRegion region;
        ImageFilter filter;
        void Execute(OpenGLRenderDevice *device) const;
    };
    struct MultiDrawCommand {
        BufferHandle buffer;
        std::uint32_t count;
        void Execute(OpenGLRenderDevice* device) const;
    };
  struct BarrierCommand
  {
    MemoryBarrierDesc desc;
    void Execute(OpenGLRenderDevice*) const;
  };
  void MemoryBarrier(const MemoryBarrierDesc& desc) override
  {
    if (!Begun) throw std::logic_error("Begin command recording first");
    m_commands.emplace_back(BarrierCommand{desc});
  }
  struct DebugCommand
  {
    std::string name;
    int operation;
    void Execute(OpenGLRenderDevice*) const;
  };
  void PushDebugGroup(std::string_view name, std::array<float, 4>) override
  {
    m_commands.emplace_back(DebugCommand{std::string(name), 0});
  }
  void PopDebugGroup() override
  {
    m_commands.emplace_back(DebugCommand{{}, 1});
  }
  void InsertDebugMarker(std::string_view name, std::array<float, 4>) override
  {
    m_commands.emplace_back(DebugCommand{std::string(name), 2});
  }
    using Command =
        std::variant<BarrierCommand, DebugCommand, StencilCommand, BeginRenderCommand, EndRenderCommand,
                     BindPipelineCommand, BindDescriptorSetCommand,
                     BindVertexBufferCommand, BindIndexBufferCommand,
                     BindTextureCommand, DrawCommand, DrawIndexedCommand,
                     MultiDrawCommand, DispatchCommand, CopyBufferCommand,
                     BlitImageCommand, GenMipMapsCommand>;

    const std::vector<Command>& Commands() const noexcept { return m_commands; }
    void BeginRendering(const RenderPassDesc& desc) override { m_rendering = true; m_commands.emplace_back(BeginRenderCommand{desc}); }
    void SetStencil(StencilMode mode, std::uint32_t reference) override { m_commands.emplace_back(StencilCommand{mode, reference}); }
    void EndRendering() override { m_rendering = false; m_commands.emplace_back(EndRenderCommand{}); }
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
    void MultiDrawIndexedIndirect(BufferHandle buffer, std::uint32_t count) override {
        m_commands.emplace_back(MultiDrawCommand{buffer, count});
    }
    void CopyBuffer(BufferHandle src, BufferHandle dst, std::uint64_t srcOffset,
                    std::uint64_t dstOffset, std::uint64_t bytes) override
    {
      m_commands.emplace_back(
          CopyBufferCommand{src, dst, srcOffset, dstOffset, bytes});
    }
    void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override { m_commands.emplace_back(DispatchCommand{x, y, z}); }
  void GenMipMaps(const GenMipMapsDesc& desc) override {
      if (!Begun) throw std::logic_error("Begin command recording first");
      if (m_rendering) throw std::logic_error("Mipmap generation must be outside rendering");
      m_commands.emplace_back(GenMipMapsCommand{desc});
  }
  void BlitImage(ImageHandle src, ImageHandle dst, const BlitRegion &region,
                 ImageFilter filter) override {
      m_commands.emplace_back(BlitImageCommand{src, dst, region, filter});
  }
private:
    std::vector<Command> m_commands;
    bool Begun = false;
    bool m_rendering = false;
    std::uint64_t m_owner;
};

} // namespace Lighthouse
