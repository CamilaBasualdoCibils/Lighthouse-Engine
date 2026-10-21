#include "OpenGLCommandList.hpp"
#include "OpenGLRenderDeviceImpl.hpp"

#include <stdexcept>

namespace Lighthouse {
namespace {
GLenum ToGLTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::Triangles: return GL_TRIANGLES;
    case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
    case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
    case PrimitiveTopology::Lines: return GL_LINES;
    case PrimitiveTopology::Points: return GL_POINTS;
    }
    throw std::invalid_argument("Unsupported primitive topology");
}

GLint VertexComponents(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float: case VertexFormat::Int: case VertexFormat::UInt: return 1;
    case VertexFormat::Float2: case VertexFormat::Int2: case VertexFormat::UInt2: return 2;
    case VertexFormat::Float3: case VertexFormat::Int3: case VertexFormat::UInt3: return 3;
    case VertexFormat::Float4: case VertexFormat::Int4: case VertexFormat::UInt4: return 4;
    case VertexFormat::UNorm8x4: case VertexFormat::SNorm8x4: return 4;
    }
    throw std::invalid_argument("Unsupported vertex format");
}

bool IsFloatFormat(VertexFormat format) {
    return format >= VertexFormat::Float && format <= VertexFormat::Float4;
}

bool IsSignedIntegerFormat(VertexFormat format) {
    return format >= VertexFormat::Int && format <= VertexFormat::Int4;
}

bool IsUnsignedIntegerFormat(VertexFormat format) {
    return format >= VertexFormat::UInt && format <= VertexFormat::UInt4;
}

GLuint CreateVertexArray(const VertexLayout& layout) {
    GLuint vao{};
    glCreateVertexArrays(1, &vao);
    for (const auto& binding : layout.bindings)
        glVertexArrayBindingDivisor(vao, binding.binding, binding.inputRate == VertexInputRate::PerInstance ? 1 : 0);
    for (const auto& attr : layout.attributes) {
        glEnableVertexArrayAttrib(vao, attr.location);
        if (IsFloatFormat(attr.format)) glVertexArrayAttribFormat(vao, attr.location, VertexComponents(attr.format), GL_FLOAT, GL_FALSE, attr.offset);
        else if (IsSignedIntegerFormat(attr.format)) glVertexArrayAttribIFormat(vao, attr.location, VertexComponents(attr.format), GL_INT, attr.offset);
        else if (IsUnsignedIntegerFormat(attr.format)) glVertexArrayAttribIFormat(vao, attr.location, VertexComponents(attr.format), GL_UNSIGNED_INT, attr.offset);
        else if (attr.format == VertexFormat::UNorm8x4) glVertexArrayAttribFormat(vao, attr.location, 4, GL_UNSIGNED_BYTE, GL_TRUE, attr.offset);
        else if (attr.format == VertexFormat::SNorm8x4) glVertexArrayAttribFormat(vao, attr.location, 4, GL_BYTE, GL_TRUE, attr.offset);
        glVertexArrayAttribBinding(vao, attr.location, attr.binding);
    }
    return vao;
}

} // namespace

void OpenGLCommandList::BeginRenderCommand::Execute(OpenGLRenderDevice* device) const {
    auto& state = *device->m_impl;
    if (state.activeFramebuffer) {
        glDeleteFramebuffers(1, &state.activeFramebuffer);
        state.activeFramebuffer = 0;
    }
    if (state.acquired && desc.colorAttachment == device->AcquiredImageView()) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        state.frameReady = true;
    } else {
        if (!desc.colorAttachment) throw std::logic_error("Render pass has no color attachment");
        glCreateFramebuffers(1, &state.activeFramebuffer);
        glNamedFramebufferTexture(state.activeFramebuffer, GL_COLOR_ATTACHMENT0,
                                  state.views.at(desc.colorAttachment), 0);
        constexpr GLenum attachment = GL_COLOR_ATTACHMENT0;
        glNamedFramebufferDrawBuffers(state.activeFramebuffer, 1, &attachment);
        if (glCheckNamedFramebufferStatus(state.activeFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &state.activeFramebuffer);
            state.activeFramebuffer = 0;
            throw std::runtime_error("OpenGL render target framebuffer is incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, state.activeFramebuffer);
    }
    glViewport(desc.offsetX, desc.offsetY, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
    if (desc.clearColor) {
        const GLfloat color[]{desc.clearColorValue.x, desc.clearColorValue.y, desc.clearColorValue.z, 1.f};
        glClearBufferfv(GL_COLOR, 0, color);
    }
    if (desc.clearDepth) glClear(GL_DEPTH_BUFFER_BIT);
}
void OpenGLCommandList::EndRenderCommand::Execute(OpenGLRenderDevice* device) const {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    auto& framebuffer = device->m_impl->activeFramebuffer;
    if (framebuffer) { glDeleteFramebuffers(1, &framebuffer); framebuffer = 0; }
}
void OpenGLCommandList::BindPipelineCommand::Execute(OpenGLRenderDevice* device) const {
    auto& pipeline = device->m_impl->pipelines.at(handle);
    const auto context = device->m_impl->acquired.value_or(SwapchainHandle{});
    auto [vao, inserted] = pipeline.vaos.try_emplace(context, 0);
    if (inserted) vao->second = CreateVertexArray(pipeline.layout);
    glBindVertexArray(vao->second);
    glUseProgram(pipeline.program);
}
void OpenGLCommandList::BindVertexBufferCommand::Execute(OpenGLRenderDevice* device) const {
    glBindVertexBuffer(0, device->m_impl->buffers.at(handle).id, 0, static_cast<GLsizei>(stride));
}
void OpenGLCommandList::BindIndexBufferCommand::Execute(OpenGLRenderDevice* device) const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, device->m_impl->buffers.at(handle).id);
}
void OpenGLCommandList::BindTextureCommand::Execute(OpenGLRenderDevice* device) const {
    glBindTextureUnit(slot, device->m_impl->images.at(handle));
}
void OpenGLCommandList::DrawCommand::Execute(OpenGLRenderDevice*) const {
    glDrawArraysInstancedBaseInstance(ToGLTopology(topology), static_cast<GLint>(first),
                                     static_cast<GLsizei>(count), static_cast<GLsizei>(instances), firstInstance);
}
void OpenGLCommandList::DrawIndexedCommand::Execute(OpenGLRenderDevice*) const {
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_INT, nullptr);
}
void OpenGLCommandList::DispatchCommand::Execute(OpenGLRenderDevice*) const {
    glDispatchCompute(x, y, z);
}

} // namespace Lighthouse
