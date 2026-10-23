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
                                  state.views.at(desc.colorAttachment).id, 0);
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
    auto found = device->m_impl->pipelines.find(handle);
    if (found == device->m_impl->pipelines.end()) throw std::invalid_argument("Invalid pipeline handle");
    auto& pipeline = found->second;
    auto& state = *device->m_impl;
    if (state.activePipeline) {
        const auto old = state.pipelines.find(*state.activePipeline);
        const auto oldLayout = old == state.pipelines.end() ? state.pipelineLayouts.end() : state.pipelineLayouts.find(old->second.pipelineLayout);
        const auto newLayout = state.pipelineLayouts.find(pipeline.pipelineLayout);
        auto layoutsCompatible = [&](DescriptorSetLayoutHandle a, DescriptorSetLayoutHandle b) {
            if (a == b) return true;
            const auto left=state.setLayouts.find(a), right=state.setLayouts.find(b);
            if(left==state.setLayouts.end() || right==state.setLayouts.end() || left->second.bindings.size()!=right->second.bindings.size()) return false;
            return std::equal(left->second.bindings.begin(),left->second.bindings.end(),right->second.bindings.begin(),[](const auto& x,const auto& y) {
                return x.first==y.first && x.second.type==y.second.type && x.second.count==y.second.count && x.second.stages==y.second.stages;
            });
        };
        for (auto it = state.boundSets.begin(); it != state.boundSets.end();) {
            const auto setIndex = it->first;
            const bool compatible = oldLayout != state.pipelineLayouts.end() && newLayout != state.pipelineLayouts.end() &&
                setIndex < oldLayout->second.sets.size() && setIndex < newLayout->second.sets.size() &&
                layoutsCompatible(oldLayout->second.sets[setIndex], newLayout->second.sets[setIndex]);
            if (compatible) { ++it; continue; }
            const auto oldSet = state.sets.find(it->second);
            if (oldSet != state.sets.end()) {
                const auto descriptorLayout = state.setLayouts.find(oldSet->second.layout);
                if (descriptorLayout != state.setLayouts.end()) for (const auto& [binding, desc] : descriptorLayout->second.bindings) {
                    const auto location = oldLayout == state.pipelineLayouts.end() ? DescriptorBindingLocations::const_iterator{} : oldLayout->second.locations.find({setIndex,binding});
                    if (oldLayout == state.pipelineLayouts.end() || location == oldLayout->second.locations.end()) continue;
                    for (std::uint32_t element=0;element<desc.count;++element) {
                        const auto point=location->second.bindingPoint+element;
                        if(desc.type==DescriptorType::UniformBuffer) glBindBufferBase(GL_UNIFORM_BUFFER,point,0);
                        else if(desc.type==DescriptorType::StorageBuffer) glBindBufferBase(GL_SHADER_STORAGE_BUFFER,point,0);
                        else { glBindTextureUnit(point,0); glBindSampler(point,0); }
                    }
                }
            }
            it=state.boundSets.erase(it);
        }
    }
    state.activePipeline = handle;
    const auto context = device->m_impl->acquired.value_or(SwapchainHandle{});
    auto [vao, inserted] = pipeline.vaos.try_emplace(context, 0);
    if (inserted) vao->second = CreateVertexArray(pipeline.layout);
    glBindVertexArray(vao->second);
    glUseProgram(pipeline.program);
}
void OpenGLCommandList::BindDescriptorSetCommand::Execute(OpenGLRenderDevice* device) const {
    auto& state = *device->m_impl;
    if (!state.activePipeline) throw std::logic_error("Bind a graphics pipeline before binding descriptor sets");
    const auto pipeline = state.pipelines.find(*state.activePipeline);
    const auto set = state.sets.find(handle);
    if (pipeline == state.pipelines.end() || set == state.sets.end()) throw std::invalid_argument("Invalid pipeline or descriptor set handle");
    const auto pipelineLayout = state.pipelineLayouts.find(pipeline->second.pipelineLayout);
    if (pipelineLayout == state.pipelineLayouts.end() || setIndex >= pipelineLayout->second.sets.size() || pipelineLayout->second.sets[setIndex] != set->second.layout)
        throw std::invalid_argument("Descriptor set layout is incompatible with the bound pipeline");
    const auto layout = state.setLayouts.find(set->second.layout);
    if (layout == state.setLayouts.end()) throw std::invalid_argument("Descriptor set layout has been destroyed");
    for (const auto& [binding, info] : layout->second.bindings) {
        const auto location = pipelineLayout->second.locations.find({setIndex, binding});
        if (location == pipelineLayout->second.locations.end()) throw std::logic_error("Pipeline layout is missing descriptor binding metadata");
        const auto& values = set->second.resources.at(binding);
        for (std::uint32_t element = 0; element < info.count; ++element) {
            const auto point = location->second.bindingPoint + element;
            const auto& resource = values[element];
            if (std::holds_alternative<std::monostate>(resource)) throw std::logic_error("Descriptor set contains an uninitialized binding");
            if (info.type == DescriptorType::UniformBuffer || info.type == DescriptorType::StorageBuffer) {
                const auto& bufferInfo = std::get<DescriptorBufferInfo>(resource);
                const auto buffer = state.buffers.find(bufferInfo.buffer);
                if (buffer == state.buffers.end()) throw std::invalid_argument("Descriptor references a destroyed buffer");
                glBindBufferRange(info.type == DescriptorType::UniformBuffer ? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER, point,
                                  buffer->second.id, static_cast<GLintptr>(bufferInfo.offset), static_cast<GLsizeiptr>(bufferInfo.range));
            } else {
                const auto& imageInfo = std::get<DescriptorImageInfo>(resource);
                const auto image = state.views.find(imageInfo.image); const auto sampler = state.samplers.find(imageInfo.sampler);
                if (image == state.views.end() || sampler == state.samplers.end()) throw std::invalid_argument("Descriptor references a destroyed image view or sampler");
                glBindTextureUnit(point, image->second.id); glBindSampler(point, sampler->second);
            }
        }
    }
    state.boundSets[setIndex] = handle;
}
void OpenGLCommandList::BindVertexBufferCommand::Execute(OpenGLRenderDevice* device) const {
    glBindVertexBuffer(0, device->m_impl->buffers.at(handle).id, 0, static_cast<GLsizei>(stride));
}
void OpenGLCommandList::BindIndexBufferCommand::Execute(OpenGLRenderDevice* device) const {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, device->m_impl->buffers.at(handle).id);
}
void OpenGLCommandList::BindTextureCommand::Execute(OpenGLRenderDevice* device) const {
    glBindTextureUnit(slot, device->m_impl->images.at(handle).id);
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
