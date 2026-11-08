#include "OpenGLCommandList.hpp"
#include "OpenGLRenderDeviceImpl.hpp"

#include <stdexcept>

namespace Lighthouse
{
namespace
{
GLenum ToGLBlendFactor(BlendFactor value)
{
  switch (value)
  {
  case BlendFactor::Zero:
    return GL_ZERO;
  case BlendFactor::One:
    return GL_ONE;
  case BlendFactor::SourceColor:
    return GL_SRC_COLOR;
  case BlendFactor::OneMinusSourceColor:
    return GL_ONE_MINUS_SRC_COLOR;
  case BlendFactor::DestinationColor:
    return GL_DST_COLOR;
  case BlendFactor::OneMinusDestinationColor:
    return GL_ONE_MINUS_DST_COLOR;
  case BlendFactor::SourceAlpha:
    return GL_SRC_ALPHA;
  case BlendFactor::OneMinusSourceAlpha:
    return GL_ONE_MINUS_SRC_ALPHA;
  case BlendFactor::DestinationAlpha:
    return GL_DST_ALPHA;
  case BlendFactor::OneMinusDestinationAlpha:
    return GL_ONE_MINUS_DST_ALPHA;
  case BlendFactor::ConstantColor:
    return GL_CONSTANT_COLOR;
  case BlendFactor::OneMinusConstantColor:
    return GL_ONE_MINUS_CONSTANT_COLOR;
  case BlendFactor::ConstantAlpha:
    return GL_CONSTANT_ALPHA;
  case BlendFactor::OneMinusConstantAlpha:
    return GL_ONE_MINUS_CONSTANT_ALPHA;
  case BlendFactor::SourceAlphaSaturate:
    return GL_SRC_ALPHA_SATURATE;
  }
  throw std::invalid_argument("Unsupported BlendFactor");
}

GLenum ToGLBlendOperation(BlendOperation value)
{
  switch (value)
  {
  case BlendOperation::Add:
    return GL_FUNC_ADD;
  case BlendOperation::Subtract:
    return GL_FUNC_SUBTRACT;
  case BlendOperation::ReverseSubtract:
    return GL_FUNC_REVERSE_SUBTRACT;
  case BlendOperation::Min:
    return GL_MIN;
  case BlendOperation::Max:
    return GL_MAX;
  }
  throw std::invalid_argument("Unsupported BlendOperation");
}

GLenum ToGLTopology(PrimitiveTopology topology)
{
  switch (topology)
  {
  case PrimitiveTopology::Triangles:
    return GL_TRIANGLES;
  case PrimitiveTopology::TriangleStrip:
    return GL_TRIANGLE_STRIP;
  case PrimitiveTopology::LineStrip:
    return GL_LINE_STRIP;
  case PrimitiveTopology::Lines:
    return GL_LINES;
  case PrimitiveTopology::Points:
    return GL_POINTS;
  }
  throw std::invalid_argument("Unsupported primitive topology");
}

GLint VertexComponents(VertexFormat format)
{
  switch (format)
  {
  case VertexFormat::Float:
  case VertexFormat::Int:
  case VertexFormat::UInt:
    return 1;
  case VertexFormat::Float2:
  case VertexFormat::Int2:
  case VertexFormat::UInt2:
    return 2;
  case VertexFormat::Float3:
  case VertexFormat::Int3:
  case VertexFormat::UInt3:
    return 3;
  case VertexFormat::Float4:
  case VertexFormat::Int4:
  case VertexFormat::UInt4:
    return 4;
  case VertexFormat::UNorm8x4:
  case VertexFormat::SNorm8x4:
    return 4;
  }
  throw std::invalid_argument("Unsupported vertex format");
}

bool IsFloatFormat(VertexFormat format)
{
  return format >= VertexFormat::Float && format <= VertexFormat::Float4;
}

bool IsSignedIntegerFormat(VertexFormat format)
{
  return format >= VertexFormat::Int && format <= VertexFormat::Int4;
}

bool IsUnsignedIntegerFormat(VertexFormat format)
{
  return format >= VertexFormat::UInt && format <= VertexFormat::UInt4;
}

GLuint CreateVertexArray(const VertexLayout& layout)
{
  GLuint vao{};
  glCreateVertexArrays(1, &vao);
  for (const auto& binding : layout.bindings)
    glVertexArrayBindingDivisor(
        vao, binding.binding,
        binding.inputRate == VertexInputRate::PerInstance ? 1 : 0);
  for (const auto& attr : layout.attributes)
  {
    glEnableVertexArrayAttrib(vao, attr.location);
    if (IsFloatFormat(attr.format))
      glVertexArrayAttribFormat(vao, attr.location,
                                VertexComponents(attr.format), GL_FLOAT,
                                GL_FALSE, attr.offset);
    else if (IsSignedIntegerFormat(attr.format))
      glVertexArrayAttribIFormat(vao, attr.location,
                                 VertexComponents(attr.format), GL_INT,
                                 attr.offset);
    else if (IsUnsignedIntegerFormat(attr.format))
      glVertexArrayAttribIFormat(vao, attr.location,
                                 VertexComponents(attr.format), GL_UNSIGNED_INT,
                                 attr.offset);
    else if (attr.format == VertexFormat::UNorm8x4)
      glVertexArrayAttribFormat(vao, attr.location, 4, GL_UNSIGNED_BYTE,
                                GL_TRUE, attr.offset);
    else if (attr.format == VertexFormat::SNorm8x4)
      glVertexArrayAttribFormat(vao, attr.location, 4, GL_BYTE, GL_TRUE,
                                attr.offset);
    glVertexArrayAttribBinding(vao, attr.location, attr.binding);
  }
  return vao;
}

} // namespace

void OpenGLCommandList::BeginRenderCommand::Execute(
    OpenGLRenderDevice* device) const
{
  auto& state = *device->m_impl;
  if (desc.viewMask)
    throw std::invalid_argument("OpenGL multiview is not enabled");
  if (desc.colorAttachment && !desc.colorAttachments.empty())
    throw std::invalid_argument(
        "Specify either colorAttachment or colorAttachments");
  const auto attachments =
      desc.colorAttachments.empty()
          ? std::vector<ImageViewHandle>{desc.colorAttachment}
          : desc.colorAttachments;
  state.ActivateView(attachments.front());
  GLint limit = 0;
  glGetIntegerv(GL_MAX_DRAW_BUFFERS, &limit);
  if (attachments.size() > static_cast<size_t>(limit))
    throw std::invalid_argument("Too many color attachments");
  for (auto view : attachments)
  {
    const auto& image = state.R().images.at(state.R().views.at(view).image);
    if (!image.usage.Contains(ImageUsage::ColorAttachment))
      throw std::invalid_argument("Color attachment usage missing");
  }
  if (state.State().activeFramebuffer)
  {
    glDeleteFramebuffers(1, &state.State().activeFramebuffer);
    state.State().activeFramebuffer = 0;
  }
  {
    if (!attachments.front())
      throw std::logic_error("Render pass has no color attachment");
    glCreateFramebuffers(1, &state.State().activeFramebuffer);
    std::vector<GLenum> drawBuffers;
    for (size_t i = 0; i < attachments.size(); ++i)
    {
      drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
      glNamedFramebufferTexture(state.State().activeFramebuffer,
                                drawBuffers.back(),
                                state.R().views.at(attachments[i]).id, 0);
    }
    glNamedFramebufferDrawBuffers(state.State().activeFramebuffer,
                                  static_cast<GLsizei>(drawBuffers.size()),
                                  drawBuffers.data());
    if (glCheckNamedFramebufferStatus(state.State().activeFramebuffer,
                                      GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE)
    {
      glDeleteFramebuffers(1, &state.State().activeFramebuffer);
      state.State().activeFramebuffer = 0;
      throw std::runtime_error(
          "OpenGL render target framebuffer is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, state.State().activeFramebuffer);
  }
  const bool stencil =
      desc.depthAttachment &&
      state.R()
              .images.at(state.R().views.at(desc.depthAttachment).image)
              .format == ImageFormat::Depth24Stencil8;
  glFramebufferTexture(
      GL_FRAMEBUFFER,
      stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
      desc.depthAttachment ? state.R().views.at(desc.depthAttachment).id : 0,
      0);
  glDisable(GL_STENCIL_TEST);
  glStencilMask(0xff);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    throw std::runtime_error("OpenGL render target attachments are incomplete");
  if (desc.depthAttachment && !desc.depthReadOnly)
  {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
  }
  else
  {
    glDisable(GL_DEPTH_TEST);
  }
  // Match the canonical Vulkan/SPIR-V depth range used by engine shaders.
  glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
  glViewport(desc.offsetX, desc.offsetY, static_cast<GLsizei>(desc.width),
             static_cast<GLsizei>(desc.height));
  if (desc.clearColor)
  {
    const GLfloat color[]{desc.clearColorValue.x, desc.clearColorValue.y,
                          desc.clearColorValue.z, desc.clearColorAlpha};
    for (size_t i = 0; i < attachments.size(); ++i)
      glClearBufferfv(GL_COLOR, static_cast<GLint>(i), color);
  }
  if (desc.clearDepth)
  {
    glClearDepth(1.0);
    glClearStencil(0);
    glClear(GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0));
  }
  for (auto view : attachments)
    state.RecordWrite(state.R().views.at(view).image);
  if (desc.depthAttachment)
    state.RecordWrite(state.R().views.at(desc.depthAttachment).image);
}
void OpenGLCommandList::StencilCommand::Execute(OpenGLRenderDevice*) const
{
  if (reference > 255)
    throw std::invalid_argument("Stencil reference exceeds 8 bits");
  if (mode == StencilMode::Disabled)
  {
    glDisable(GL_STENCIL_TEST);
    return;
  }
  glEnable(GL_STENCIL_TEST);
  glStencilMask(mode == StencilMode::Write ? 0xff : 0);
  glStencilFunc(mode == StencilMode::Equal ? GL_EQUAL : GL_ALWAYS, reference,
                0xff);
  glStencilOp(GL_KEEP, GL_KEEP,
              mode == StencilMode::Write ? GL_REPLACE : GL_KEEP);
}
void OpenGLCommandList::EndRenderCommand::Execute(
    OpenGLRenderDevice* device) const
{
  // The depth image may be a transient graph resource, including on a
  // persistent swapchain framebuffer. Release its attachment before graph free.
  glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, 0, 0);
  glDisable(GL_DEPTH_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  auto& framebuffer = device->m_impl->State().activeFramebuffer;
  if (framebuffer)
  {
    glDeleteFramebuffers(1, &framebuffer);
    framebuffer = 0;
  }
}
void OpenGLCommandList::BindPipelineCommand::Execute(
    OpenGLRenderDevice* device) const
{
  auto found = device->m_impl->R().pipelines.find(handle);
  if (found == device->m_impl->R().pipelines.end())
    throw std::invalid_argument("Invalid pipeline handle");
  auto& pipeline = found->second;
  auto& state = *device->m_impl;
  if (state.State().activePipeline)
  {
    const auto old = state.R().pipelines.find(*state.State().activePipeline);
    const auto oldLayout =
        old == state.R().pipelines.end()
            ? state.R().pipelineLayouts.end()
            : state.R().pipelineLayouts.find(old->second.pipelineLayout);
    const auto newLayout =
        state.R().pipelineLayouts.find(pipeline.pipelineLayout);
    auto layoutsCompatible =
        [&](DescriptorSetLayoutHandle a, DescriptorSetLayoutHandle b)
    {
      if (a == b)
        return true;
      const auto left = state.R().setLayouts.find(a),
                 right = state.R().setLayouts.find(b);
      if (left == state.R().setLayouts.end() ||
          right == state.R().setLayouts.end() ||
          left->second.bindings.size() != right->second.bindings.size())
        return false;
      return std::equal(left->second.bindings.begin(),
                        left->second.bindings.end(),
                        right->second.bindings.begin(),
                        [](const auto& x, const auto& y)
                        {
                          return x.first == y.first &&
                                 x.second.type == y.second.type &&
                                 x.second.count == y.second.count &&
                                 x.second.stages == y.second.stages;
                        });
    };
    for (auto it = state.State().boundSets.begin();
         it != state.State().boundSets.end();)
    {
      const auto setIndex = it->first;
      const bool compatible =
          oldLayout != state.R().pipelineLayouts.end() &&
          newLayout != state.R().pipelineLayouts.end() &&
          setIndex < oldLayout->second.sets.size() &&
          setIndex < newLayout->second.sets.size() &&
          layoutsCompatible(oldLayout->second.sets[setIndex],
                            newLayout->second.sets[setIndex]);
      if (compatible)
      {
        ++it;
        continue;
      }
      const auto oldSet = state.R().sets.find(it->second);
      if (oldSet != state.R().sets.end())
      {
        const auto descriptorLayout =
            state.R().setLayouts.find(oldSet->second.layout);
        if (descriptorLayout != state.R().setLayouts.end())
          for (const auto& [binding, desc] : descriptorLayout->second.bindings)
          {
            const auto location =
                oldLayout == state.R().pipelineLayouts.end()
                    ? DescriptorBindingLocations::const_iterator{}
                    : oldLayout->second.locations.find({setIndex, binding});
            if (oldLayout == state.R().pipelineLayouts.end() ||
                location == oldLayout->second.locations.end())
              continue;
            for (std::uint32_t element = 0; element < desc.count; ++element)
            {
              const auto point = location->second.bindingPoint + element;
              if (desc.type == DescriptorType::UniformBuffer)
                glBindBufferBase(GL_UNIFORM_BUFFER, point, 0);
              else if (desc.type == DescriptorType::StorageBuffer)
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, point, 0);
              else
              {
                glBindTextureUnit(point, 0);
                glBindSampler(point, 0);
              }
            }
          }
      }
      it = state.State().boundSets.erase(it);
    }
  }
  state.State().activePipeline = handle;
  auto [vao, inserted] = state.State().vaos.try_emplace(handle, 0);
  if (inserted)
    vao->second = CreateVertexArray(pipeline.layout);
  glBindVertexArray(vao->second);
  glUseProgram(pipeline.program);
  if (pipeline.blend.enabled)
  {
    glEnable(GL_BLEND);
    const auto& blend = pipeline.blend;
    glBlendEquationSeparate(ToGLBlendOperation(blend.colorOperation),
                            ToGLBlendOperation(blend.alphaOperation));
    glBlendFuncSeparate(ToGLBlendFactor(blend.sourceColorFactor),
                        ToGLBlendFactor(blend.destinationColorFactor),
                        ToGLBlendFactor(blend.sourceAlphaFactor),
                        ToGLBlendFactor(blend.destinationAlphaFactor));
    glBlendColor(blend.constants.x, blend.constants.y, blend.constants.z,
                 blend.constants.w);
  }
  else
    glDisable(GL_BLEND);
}
void OpenGLCommandList::BindDescriptorSetCommand::Execute(
    OpenGLRenderDevice* device) const
{
  auto& state = *device->m_impl;
  if (!state.State().activePipeline)
    throw std::logic_error(
        "Bind a graphics pipeline before binding descriptor sets");
  const auto pipeline = state.R().pipelines.find(*state.State().activePipeline);
  const auto set = state.R().sets.find(handle);
  if (pipeline == state.R().pipelines.end() || set == state.R().sets.end())
    throw std::invalid_argument("Invalid pipeline or descriptor set handle");
  const auto pipelineLayout =
      state.R().pipelineLayouts.find(pipeline->second.pipelineLayout);
  if (pipelineLayout == state.R().pipelineLayouts.end() ||
      setIndex >= pipelineLayout->second.sets.size() ||
      pipelineLayout->second.sets[setIndex] != set->second.layout)
    throw std::invalid_argument(
        "Descriptor set layout is incompatible with the bound pipeline");
  const auto layout = state.R().setLayouts.find(set->second.layout);
  if (layout == state.R().setLayouts.end())
    throw std::invalid_argument("Descriptor set layout has been destroyed");
  for (const auto& [binding, info] : layout->second.bindings)
  {
    const auto location =
        pipelineLayout->second.locations.find({setIndex, binding});
    if (location == pipelineLayout->second.locations.end())
      throw std::logic_error(
          "Pipeline layout is missing descriptor binding metadata");
    const auto& values = set->second.resources.at(binding);
    for (std::uint32_t element = 0; element < info.count; ++element)
    {
      const auto point = location->second.bindingPoint + element;
      const auto& resource = values[element];
      if (std::holds_alternative<std::monostate>(resource))
        throw std::logic_error(
            "Descriptor set contains an uninitialized binding");
      if (info.type == DescriptorType::UniformBuffer ||
          info.type == DescriptorType::StorageBuffer)
      {
        const auto& bufferInfo = std::get<DescriptorBufferInfo>(resource);
        const auto buffer = state.R().buffers.find(bufferInfo.buffer);
        if (buffer == state.R().buffers.end())
          throw std::invalid_argument(
              "Descriptor references a destroyed buffer");
        glBindBufferRange(info.type == DescriptorType::UniformBuffer
                              ? GL_UNIFORM_BUFFER
                              : GL_SHADER_STORAGE_BUFFER,
                          point, buffer->second.id,
                          static_cast<GLintptr>(bufferInfo.offset),
                          static_cast<GLsizeiptr>(bufferInfo.range));
      }
      else
      {
        const auto& imageInfo = std::get<DescriptorImageInfo>(resource);
        const auto image = state.R().views.find(imageInfo.image);
        const auto sampler = state.R().samplers.find(imageInfo.sampler);
        if (image == state.R().views.end() ||
            sampler == state.R().samplers.end())
          throw std::invalid_argument(
              "Descriptor references a destroyed image view or sampler");
        glBindTextureUnit(point, image->second.id);
        glBindSampler(point, sampler->second);
      }
    }
  }
  state.State().boundSets[setIndex] = handle;
}
void OpenGLCommandList::BindVertexBufferCommand::Execute(
    OpenGLRenderDevice* device) const
{
  glBindVertexBuffer(0, device->m_impl->R().buffers.at(handle).id, 0,
                     static_cast<GLsizei>(stride));
}
void OpenGLCommandList::BindIndexBufferCommand::Execute(
    OpenGLRenderDevice* device) const
{
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
               device->m_impl->R().buffers.at(handle).id);
}
void OpenGLCommandList::BindTextureCommand::Execute(
    OpenGLRenderDevice* device) const
{
  glBindTextureUnit(slot, device->m_impl->R().images.at(handle).id);
}
void OpenGLCommandList::DrawCommand::Execute(OpenGLRenderDevice*) const
{
  glDrawArraysInstancedBaseInstance(
      ToGLTopology(topology), static_cast<GLint>(first),
      static_cast<GLsizei>(count), static_cast<GLsizei>(instances),
      firstInstance);
}
void OpenGLCommandList::DrawIndexedCommand::Execute(
    OpenGLRenderDevice* device) const
{
  glDrawElementsBaseVertex(
      ToGLTopology(device->m_impl->R()
                       .pipelines.at(*device->m_impl->State().activePipeline)
                       .topology),
      static_cast<GLsizei>(count), GL_UNSIGNED_INT,
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(firstIndex) *
                                    sizeof(std::uint32_t)),
      baseVertex);
}
void OpenGLCommandList::MultiDrawCommand::Execute(
    OpenGLRenderDevice* device) const
{
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER,
               device->m_impl->R().buffers.at(buffer).id);
  glMultiDrawElementsIndirect(
      ToGLTopology(device->m_impl->R()
                       .pipelines.at(*device->m_impl->State().activePipeline)
                       .topology),
      GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count), 20);
  glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}
void OpenGLCommandList::DebugCommand::Execute(OpenGLRenderDevice* device) const
{
  if (operation == 0)
  {
    glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0,
                     static_cast<GLsizei>(name.size()), name.data());
    device->m_impl->State().debugGroups.push_back(name);
  }
  else if (operation == 1)
  {
    glPopDebugGroup();
    if (!device->m_impl->State().debugGroups.empty())
      device->m_impl->State().debugGroups.pop_back();
  }
  else
    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
                         GL_DEBUG_SEVERITY_NOTIFICATION,
                         static_cast<GLsizei>(name.size()), name.data());
}
void OpenGLCommandList::CopyBufferCommand::Execute(
    OpenGLRenderDevice* device) const
{
  auto& a = device->m_impl->R().buffers.at(src);
  auto& b = device->m_impl->R().buffers.at(dst);
  if (src == dst || bytes == 0 || ((srcOffset | dstOffset | bytes) & 3) ||
      !a.metadata.usage.Contains(BufferUsage::TransferSrc) ||
      !b.metadata.usage.Contains(BufferUsage::TransferDst) ||
      srcOffset > a.metadata.size || bytes > a.metadata.size - srcOffset ||
      dstOffset > b.metadata.size || bytes > b.metadata.size - dstOffset)
    throw std::invalid_argument("Invalid buffer copy");
  glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
  glCopyNamedBufferSubData(a.id, b.id, srcOffset, dstOffset, bytes);
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
}
void OpenGLCommandList::DispatchCommand::Execute(OpenGLRenderDevice*) const
{
  glDispatchCompute(x, y, z);
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
}
void OpenGLCommandList::BlitImageCommand::Execute(
    OpenGLRenderDevice* device) const
{
  auto& state = *device->m_impl;
  // FBOs are context-local, even though their texture attachments are shared.
  if (!state.ActivateImage(dst))
    state.ActivateImage(src);
  const auto srcImage = state.R().images.find(src);
  const auto dstImage = state.R().images.find(dst);
  if (srcImage == state.R().images.end() || dstImage == state.R().images.end())
    throw std::invalid_argument("Invalid image handle in blit command");

  const auto aspects = region.srcSubresource.aspectMask;
  if (*aspects != *region.dstSubresource.aspectMask || aspects.Empty() ||
      region.srcSubresource.layerCount != region.dstSubresource.layerCount ||
      !region.srcSubresource.layerCount)
    throw std::invalid_argument("Blit aspects and layer counts must match");
  const bool color = aspects.Contains(ImageAspect::Color);
  if (color && (aspects.Contains(ImageAspect::Depth) ||
                aspects.Contains(ImageAspect::Stencil)))
    throw std::invalid_argument(
        "Color and depth/stencil cannot be blitted together");
  if (!color && (filter != ImageFilter::Nearest ||
                 srcImage->second.format != dstImage->second.format))
    throw std::invalid_argument(
        "Depth/stencil blits require nearest filtering and matching formats");
  const GLenum attachment = color ? GL_COLOR_ATTACHMENT0
                            : aspects.Contains(ImageAspect::Depth)
                                ? (aspects.Contains(ImageAspect::Stencil)
                                       ? GL_DEPTH_STENCIL_ATTACHMENT
                                       : GL_DEPTH_ATTACHMENT)
                                : GL_STENCIL_ATTACHMENT;
  GLuint framebuffers[2]{};
  glCreateFramebuffers(2, framebuffers);
  glNamedFramebufferReadBuffer(framebuffers[0],
                               color ? GL_COLOR_ATTACHMENT0 : GL_NONE);
  const GLenum drawBuffer = color ? GL_COLOR_ATTACHMENT0 : GL_NONE;
  glNamedFramebufferDrawBuffers(framebuffers[1], 1, &drawBuffer);
  const auto attach = [attachment](GLuint framebuffer, const auto& image,
                                   const auto& subresource, GLint layer)
  {
    if (image.type == ImageType::Image1D || image.type == ImageType::Image2D)
      glNamedFramebufferTexture(framebuffer, attachment, image.id,
                                static_cast<GLint>(subresource.mipLevel));
    else
      glNamedFramebufferTextureLayer(framebuffer, attachment, image.id,
                                     static_cast<GLint>(subresource.mipLevel),
                                     layer);
  };
  const auto& src0 = region.srcOffsets[0];
  const auto& src1 = region.srcOffsets[1];
  const auto& dst0 = region.dstOffsets[0];
  const auto& dst1 = region.dstOffsets[1];
  GLbitfield mask = 0;
  if (color)
    mask |= GL_COLOR_BUFFER_BIT;
  if (aspects.Contains(ImageAspect::Depth))
    mask |= GL_DEPTH_BUFFER_BIT;
  if (aspects.Contains(ImageAspect::Stencil))
    mask |= GL_STENCIL_BUFFER_BIT;
  for (unsigned layer = 0; layer < region.srcSubresource.layerCount; ++layer)
  {
    attach(framebuffers[0], srcImage->second, region.srcSubresource,
           static_cast<GLint>(region.srcSubresource.baseArrayLayer + layer));
    attach(framebuffers[1], dstImage->second, region.dstSubresource,
           static_cast<GLint>(region.dstSubresource.baseArrayLayer + layer));
    if (glCheckNamedFramebufferStatus(framebuffers[0], GL_READ_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE ||
        glCheckNamedFramebufferStatus(framebuffers[1], GL_DRAW_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE)
    {
      glDeleteFramebuffers(2, framebuffers);
      throw std::runtime_error("OpenGL blit attachments are incomplete");
    }
    glBlitNamedFramebuffer(
        framebuffers[0], framebuffers[1], static_cast<GLint>(src0.x),
        static_cast<GLint>(src0.y), static_cast<GLint>(src1.x),
        static_cast<GLint>(src1.y), static_cast<GLint>(dst0.x),
        static_cast<GLint>(dst0.y), static_cast<GLint>(dst1.x),
        static_cast<GLint>(dst1.y), mask,
        filter == ImageFilter::Linear ? GL_LINEAR : GL_NEAREST);
  }
  glDeleteFramebuffers(2, framebuffers);
  state.RecordWrite(dst);
}

} // namespace Lighthouse
