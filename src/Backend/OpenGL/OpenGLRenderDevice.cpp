#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include "OpenGLDeviceGroup.hpp"

#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <EGL/egl.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

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

GLenum ToGLShaderStage(ShaderStageFlags stage) {
    switch (stage) {
    case ShaderStageFlags::Vertex: return GL_VERTEX_SHADER;
    case ShaderStageFlags::Fragment: return GL_FRAGMENT_SHADER;
    case ShaderStageFlags::Compute: return GL_COMPUTE_SHADER;
    }
    throw std::invalid_argument("Unsupported shader stage");
}

GLenum ToGLImageFormat(ImageFormat format) {
    switch (format) {
    case ImageFormat::R8: return GL_R8;
    case ImageFormat::RG8: return GL_RG8;
    case ImageFormat::RGBA8: return GL_RGBA8;
    case ImageFormat::RGBA8Srgb: return GL_SRGB8_ALPHA8;
    case ImageFormat::Depth16: return GL_DEPTH_COMPONENT16;
    case ImageFormat::Depth24Stencil8: return GL_DEPTH24_STENCIL8;
    case ImageFormat::Depth32: return GL_DEPTH_COMPONENT32F;
    case ImageFormat::Undefined: break;
    }
    throw std::invalid_argument("Unsupported image format");
}

GLenum ToGLImageType(ImageType type) {
    switch (type) {
    case ImageType::Image2D: return GL_TEXTURE_2D;
    case ImageType::Image1D: return GL_TEXTURE_1D;
    case ImageType::Image3D: return GL_TEXTURE_3D;
    case ImageType::Image2DArray: return GL_TEXTURE_2D_ARRAY;
    case ImageType::Cube: return GL_TEXTURE_CUBE_MAP;
    case ImageType::CubeArray: return GL_TEXTURE_CUBE_MAP_ARRAY;
    }
    throw std::invalid_argument("Unsupported image type");
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

template<class HandleType>
HandleType MakeHandle(std::uint32_t index, std::uint64_t owner) { return HandleType{index, 1, owner}; }

template<class HandleType>
struct HandleHash { std::size_t operator()(HandleType handle) const noexcept { return handle.index; } };

} // namespace

struct OpenGLRenderDevice::Impl {
    explicit Impl(std::shared_ptr<Detail::OpenGLDeviceGroup> deviceGroup, std::uint64_t deviceId)
        : group(std::move(deviceGroup)), owner(deviceId) {}

    struct Swapchain {
        EGLSurface surface{EGL_NO_SURFACE};
        EGLContext context{EGL_NO_CONTEXT};
    };
    struct SwapchainSlot { std::uint32_t generation{1}; std::unique_ptr<Swapchain> value; };
    struct BufferResource { GLuint id{}; BufferMetadata metadata; };

    const Swapchain* FindSwapchain(SwapchainHandle handle) const noexcept {
        if (!handle || handle.owner != owner || handle.index >= swapchains.size()) return nullptr;
        const auto& slot = swapchains[handle.index];
        return slot.generation == handle.generation ? slot.value.get() : nullptr;
    }

    void ActivateContext(const Swapchain* requested = nullptr) const {
        if (!group || group->display == EGL_NO_DISPLAY)
            throw std::logic_error("Initialize an EGL display by creating a swapchain before using the OpenGL device");
        const auto context = requested ? requested->context : group->shareContext;
        const auto surface = requested ? requested->surface : group->pbuffer;
        if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE)
            throw std::logic_error("The OpenGL device has no active EGL context");
        if (eglGetCurrentContext() == context) return;
        if (eglMakeCurrent(group->display, surface, surface, context) != EGL_TRUE)
            throw std::runtime_error("Failed to make the EGL OpenGL context current");
    }

    std::shared_ptr<Detail::OpenGLDeviceGroup> group;
    std::uint64_t owner{};
    std::vector<SwapchainSlot> swapchains;
    GLuint activeFramebuffer{};
    std::uint32_t nextImage{1}, nextView{1}, nextBuffer{1}, nextShader{1}, nextPipeline{1};
    std::unordered_map<ImageHandle, GLuint, HandleHash<ImageHandle>> images;
    std::unordered_map<ImageViewHandle, GLuint, HandleHash<ImageViewHandle>> views;
    std::unordered_map<BufferHandle, BufferResource, HandleHash<BufferHandle>> buffers;
    std::unordered_map<ShaderModuleHandle, GLuint, HandleHash<ShaderModuleHandle>> shaders;
    struct Pipeline {
        GLuint program{};
        VertexLayout layout;
        std::unordered_map<SwapchainHandle, GLuint, HandleHash<SwapchainHandle>> vaos;
    };
    std::unordered_map<GraphicsPipelineHandle, Pipeline, HandleHash<GraphicsPipelineHandle>> pipelines;
};

namespace {
class OpenGLCommandList final : public IRenderCommandList {
public:
    struct Begin { RenderPassDesc desc; };
    struct End {};
    struct BindPipe { GraphicsPipelineHandle handle; };
    struct BindVertex { BufferHandle handle; std::uint32_t stride; };
    struct BindIndex { BufferHandle handle; };
    struct BindTextureCmd { std::uint32_t slot; ImageHandle handle; };
    struct DrawCmd { PrimitiveTopology topology; std::uint32_t count, first, instances, firstInstance; };
    struct DrawIndexedCmd { std::uint32_t count; };
    struct DispatchCmd { std::uint32_t x, y, z; };
    using Command = std::variant<Begin, End, BindPipe, BindVertex, BindIndex,
                                 BindTextureCmd, DrawCmd, DrawIndexedCmd, DispatchCmd>;
    std::vector<Command> commands;

    void BeginRendering(const RenderPassDesc& d) override { commands.emplace_back(Begin{d}); }
    void EndRendering() override { commands.emplace_back(End{}); }
    void BindPipeline(GraphicsPipelineHandle h) override { commands.emplace_back(BindPipe{h}); }
    void BindVertexBuffer(BufferHandle h, std::uint32_t s) override { commands.emplace_back(BindVertex{h, s}); }
    void BindIndexBuffer(BufferHandle h) override { commands.emplace_back(BindIndex{h}); }
    void BindTexture(std::uint32_t s, ImageHandle h) override { commands.emplace_back(BindTextureCmd{s, h}); }
    void Draw(PrimitiveTopology t, std::uint32_t n, std::uint32_t first,
              std::uint32_t instances, std::uint32_t firstInstance) override {
        commands.emplace_back(DrawCmd{t, n, first, instances, firstInstance});
    }
    void DrawIndexed(std::uint32_t n) override { commands.emplace_back(DrawIndexedCmd{n}); }
    void Dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) override { commands.emplace_back(DispatchCmd{x,y,z}); }
};
} // namespace

OpenGLRenderDevice::OpenGLRenderDevice(std::shared_ptr<Detail::OpenGLDeviceGroup> group,
                                       const DeviceConfig&)
    : m_impl(std::make_unique<Impl>(std::move(group), DeviceId())) {}
OpenGLRenderDevice::~OpenGLRenderDevice() {
    if (!m_impl || !m_impl->group || m_impl->group->display == EGL_NO_DISPLAY) return;
    m_impl->ActivateContext();
    for (const auto& [_, p] : m_impl->pipelines) glDeleteProgram(p.program);
    for (std::uint32_t i = 0; i < m_impl->swapchains.size(); ++i) {
        auto& slot = m_impl->swapchains[i];
        if (!slot.value) continue;
        m_impl->ActivateContext(slot.value.get());
        const SwapchainHandle handle{i, slot.generation, DeviceId()};
        for (auto& [_, pipeline] : m_impl->pipelines) {
            if (const auto vao = pipeline.vaos.find(handle); vao != pipeline.vaos.end()) {
                glDeleteVertexArrays(1, &vao->second);
                pipeline.vaos.erase(vao);
            }
        }
    }
    m_impl->ActivateContext();
    for (const auto& [_, s] : m_impl->shaders) glDeleteShader(s);
    for (const auto& [_, b] : m_impl->buffers) glDeleteBuffers(1, &b.id);
    for (const auto& [_, v] : m_impl->views) glDeleteTextures(1, &v);
    for (const auto& [_, i] : m_impl->images) glDeleteTextures(1, &i);
    for (auto& slot : m_impl->swapchains) if (slot.value) {
        if (eglGetCurrentContext() == slot.value->context)
            eglMakeCurrent(m_impl->group->display, m_impl->group->pbuffer, m_impl->group->pbuffer, m_impl->group->shareContext);
        eglDestroyContext(m_impl->group->display, slot.value->context);
        eglDestroySurface(m_impl->group->display, slot.value->surface);
        slot.value.reset();
    }
}

SwapchainHandle OpenGLRenderDevice::CreateSwapchain(Window& window, const SwapchainConfig& config) {
    if (!window.NativeDisplayHandle() || !window.NativeEGLWindowHandle())
        throw std::invalid_argument("OpenGL swapchain requires an EGL-enabled window from WindowManager");
    m_impl->group->Initialize(window.NativeDisplayHandle());
    if (m_impl->group->nativeDisplay != window.NativeDisplayHandle())
        throw std::invalid_argument("All swapchains on an OpenGL device must use the same EGL display");
    auto state = std::make_unique<Impl::Swapchain>();
    state->surface = m_impl->group->CreateWindowSurface(window.NativeEGLWindowHandle());
    try {
        state->context = m_impl->group->CreateSharedContext();
        m_impl->ActivateContext(state.get());
        if (eglSwapInterval(m_impl->group->display, config.vsync ? 1 : 0) != EGL_TRUE)
            throw std::runtime_error("Failed to configure EGL swap interval");
    } catch (...) {
        if (state->context != EGL_NO_CONTEXT) {
            eglMakeCurrent(m_impl->group->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(m_impl->group->display, state->context);
        }
        eglDestroySurface(m_impl->group->display, state->surface);
        throw;
    }
    std::uint32_t index = 0;
    while (index < m_impl->swapchains.size() && m_impl->swapchains[index].value) ++index;
    if (index == m_impl->swapchains.size()) m_impl->swapchains.emplace_back();
    auto& slot = m_impl->swapchains[index];
    slot.value = std::move(state);
    return {index, slot.generation, DeviceId()};
}

bool OpenGLRenderDevice::DestroySwapchain(SwapchainHandle handle) {
    if (!handle || handle.owner != DeviceId() || handle.index >= m_impl->swapchains.size()) return false;
    auto& slot = m_impl->swapchains[handle.index];
    if (!slot.value || slot.generation != handle.generation) return false;
    m_impl->ActivateContext(slot.value.get());
    for (auto& [_, pipeline] : m_impl->pipelines) {
        if (const auto vao = pipeline.vaos.find(handle); vao != pipeline.vaos.end()) {
            glDeleteVertexArrays(1, &vao->second);
            pipeline.vaos.erase(vao);
        }
    }
    if (eglGetCurrentContext() == slot.value->context)
        eglMakeCurrent(m_impl->group->display, m_impl->group->pbuffer, m_impl->group->pbuffer, m_impl->group->shareContext);
    eglDestroyContext(m_impl->group->display, slot.value->context);
    eglDestroySurface(m_impl->group->display, slot.value->surface);
    slot.value.reset();
    if (++slot.generation == 0) ++slot.generation;
    return true;
}
bool OpenGLRenderDevice::IsValid(SwapchainHandle handle) const noexcept { return m_impl->FindSwapchain(handle) != nullptr; }

ImageHandle OpenGLRenderDevice::CreateImage(const ImageDesc& desc) {
    m_impl->ActivateContext();
    const auto target = ToGLImageType(desc.type);
    const auto format = ToGLImageFormat(desc.format);
    GLuint texture{};
    glCreateTextures(target, 1, &texture);
    if (desc.type == ImageType::Image2D) glTextureStorage2D(texture, static_cast<GLsizei>(desc.mipLevels), format, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
    else if (desc.type == ImageType::Image3D || desc.type == ImageType::Image2DArray || desc.type == ImageType::CubeArray)
        glTextureStorage3D(texture, static_cast<GLsizei>(desc.mipLevels), format, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), static_cast<GLsizei>(desc.depth * desc.arrayLayers));
    else if (desc.type == ImageType::Image1D) glTextureStorage1D(texture, static_cast<GLsizei>(desc.mipLevels), format, static_cast<GLsizei>(desc.width));
    else glTextureStorage2D(texture, static_cast<GLsizei>(desc.mipLevels), format, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
    if (!desc.initialData.empty() && desc.type == ImageType::Image2D)
        glTextureSubImage2D(texture, 0, 0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), GL_RGBA, GL_UNSIGNED_BYTE, desc.initialData.data());
    const auto handle = MakeHandle<ImageHandle>(m_impl->nextImage++, DeviceId());
    m_impl->images.emplace(handle, texture);
    return handle;
}
ImageViewHandle OpenGLRenderDevice::CreateImageView(const ImageViewDesc& desc) {
    m_impl->ActivateContext();
    const auto source = m_impl->images.at(desc.image);
    GLuint view{};
    glGenTextures(1, &view);
    glTextureView(view, ToGLImageType(desc.type), source, ToGLImageFormat(desc.format),
                  desc.baseMipLevel, desc.mipLevelCount, desc.baseArrayLayer, desc.arrayLayerCount);
    const auto handle = MakeHandle<ImageViewHandle>(m_impl->nextView++, DeviceId());
    m_impl->views.emplace(handle, view);
    return handle;
}

BufferHandle OpenGLRenderDevice::CreateBuffer(const BufferDesc& desc) {
    m_impl->ActivateContext();
    if (desc.initialData.size_bytes() > desc.size) throw std::invalid_argument("Buffer initial data exceeds buffer size");
    GLuint id{}; glCreateBuffers(1, &id);
    glNamedBufferStorage(id, static_cast<GLsizeiptr>(desc.size), desc.initialData.empty() ? nullptr : desc.initialData.data(), 0);
    const auto handle = MakeHandle<BufferHandle>(m_impl->nextBuffer++, DeviceId());
    m_impl->buffers.emplace(handle, Impl::BufferResource{id, BufferMetadata{desc.size, desc.usage, desc.memory}});
    return handle;
}
std::optional<BufferMetadata> OpenGLRenderDevice::GetBufferDesc(BufferHandle handle) const noexcept {
    const auto it = m_impl->buffers.find(handle);
    if (it == m_impl->buffers.end()) return std::nullopt;
    return it->second.metadata;
}
bool OpenGLRenderDevice::IsValid(BufferHandle handle) const noexcept { return m_impl->buffers.contains(handle); }

ShaderModuleHandle OpenGLRenderDevice::CreateShaderModule(const ShaderModuleDesc& desc) {
    m_impl->ActivateContext();
    const auto shader = glCreateShader(ToGLShaderStage(desc.stage));
    const auto* source = reinterpret_cast<const GLchar*>(desc.bytecode.data());
    const auto length = static_cast<GLint>(desc.bytecode.size_bytes());
    glShaderSource(shader, 1, &source, &length); glCompileShader(shader);
    GLint ok{}; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint size{}; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0'); glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader); throw std::runtime_error("OpenGL shader compilation failed: " + log);
    }
    const auto handle = MakeHandle<ShaderModuleHandle>(m_impl->nextShader++, DeviceId()); m_impl->shaders.emplace(handle, shader); return handle;
}

GraphicsPipelineHandle OpenGLRenderDevice::CreatePipeline(const GraphicsPipelineDesc& desc) {
    m_impl->ActivateContext();
    const auto vert = m_impl->shaders.at(desc.vertexShader), frag = m_impl->shaders.at(desc.fragmentShader);
    const auto program = glCreateProgram(); glAttachShader(program, vert); glAttachShader(program, frag); glLinkProgram(program);
    GLint ok{}; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) { GLint size{}; glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size); std::string log(static_cast<std::size_t>(std::max(size,1)), '\0'); glGetProgramInfoLog(program,size,nullptr,log.data()); glDeleteProgram(program); throw std::runtime_error("OpenGL pipeline link failed: " + log); }
    const auto handle = MakeHandle<GraphicsPipelineHandle>(m_impl->nextPipeline++, DeviceId());
    m_impl->pipelines.emplace(handle, Impl::Pipeline{program, desc.vertexLayout, {}});
    return handle;
}

std::unique_ptr<IRenderCommandList> OpenGLRenderDevice::CreateCommandList(QueueType) { return std::make_unique<OpenGLCommandList>(); }
void OpenGLRenderDevice::Destroy(ImageHandle h) { m_impl->ActivateContext(); auto i=m_impl->images.find(h); if(i!=m_impl->images.end()){glDeleteTextures(1,&i->second);m_impl->images.erase(i);} }
void OpenGLRenderDevice::Destroy(ImageViewHandle h) { m_impl->ActivateContext(); auto i=m_impl->views.find(h); if(i!=m_impl->views.end()){glDeleteTextures(1,&i->second);m_impl->views.erase(i);} }
void OpenGLRenderDevice::Destroy(BufferHandle h) { m_impl->ActivateContext(); auto i=m_impl->buffers.find(h); if(i!=m_impl->buffers.end()){glDeleteBuffers(1,&i->second.id);m_impl->buffers.erase(i);} }
void OpenGLRenderDevice::Destroy(GraphicsPipelineHandle h) {
    auto i = m_impl->pipelines.find(h);
    if (i == m_impl->pipelines.end()) return;
    m_impl->ActivateContext();
    glDeleteProgram(i->second.program);
    for (std::uint32_t index = 0; index < m_impl->swapchains.size(); ++index) {
        auto& slot = m_impl->swapchains[index];
        if (!slot.value) continue;
        const SwapchainHandle swapchain{index, slot.generation, DeviceId()};
        if (const auto vao = i->second.vaos.find(swapchain); vao != i->second.vaos.end()) {
            m_impl->ActivateContext(slot.value.get());
            glDeleteVertexArrays(1, &vao->second);
            i->second.vaos.erase(vao);
        }
    }
    m_impl->ActivateContext();
    m_impl->pipelines.erase(i);
}
void OpenGLRenderDevice::Destroy(ShaderModuleHandle h) { m_impl->ActivateContext(); auto i=m_impl->shaders.find(h); if(i!=m_impl->shaders.end()){glDeleteShader(i->second);m_impl->shaders.erase(i);} }
RenderCapabilities OpenGLRenderDevice::GetCapabilities() const { return {}; }

void OpenGLRenderDevice::Submit(SwapchainHandle swapchain, const IRenderCommandList& list) {
    const auto* target = m_impl->FindSwapchain(swapchain);
    if (!target) throw std::invalid_argument("OpenGL submit received an invalid swapchain handle");
    m_impl->ActivateContext(target);
    const auto& commands = dynamic_cast<const OpenGLCommandList&>(list).commands;
    for (const auto& command : commands) std::visit([&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, OpenGLCommandList::Begin>) {
            if (m_impl->activeFramebuffer) glDeleteFramebuffers(1, &m_impl->activeFramebuffer);
            m_impl->activeFramebuffer = 0;
            if (c.desc.colorAttachment) {
                glCreateFramebuffers(1, &m_impl->activeFramebuffer);
                glNamedFramebufferTexture(m_impl->activeFramebuffer, GL_COLOR_ATTACHMENT0, m_impl->views.at(c.desc.colorAttachment), 0);
                constexpr GLenum target = GL_COLOR_ATTACHMENT0;
                glNamedFramebufferDrawBuffers(m_impl->activeFramebuffer, 1, &target);
                if (glCheckNamedFramebufferStatus(m_impl->activeFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    glDeleteFramebuffers(1, &m_impl->activeFramebuffer);
                    m_impl->activeFramebuffer = 0;
                    throw std::runtime_error("OpenGL render target framebuffer is incomplete");
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, m_impl->activeFramebuffer);
            glViewport(c.desc.offsetX, c.desc.offsetY, static_cast<GLsizei>(c.desc.width), static_cast<GLsizei>(c.desc.height));
            if (c.desc.clearColor) { const GLfloat color[]{c.desc.clearColorValue.x,c.desc.clearColorValue.y,c.desc.clearColorValue.z,1.f}; glClearBufferfv(GL_COLOR,0,color); }
            if (c.desc.clearDepth) glClear(GL_DEPTH_BUFFER_BIT);
        } else if constexpr (std::is_same_v<T, OpenGLCommandList::End>) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (m_impl->activeFramebuffer) { glDeleteFramebuffers(1, &m_impl->activeFramebuffer); m_impl->activeFramebuffer = 0; }
        }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::BindPipe>) {
            auto& pipeline = m_impl->pipelines.at(c.handle);
            auto [vao, inserted] = pipeline.vaos.try_emplace(swapchain, 0);
            if (inserted) vao->second = CreateVertexArray(pipeline.layout);
            glBindVertexArray(vao->second);
            glUseProgram(pipeline.program);
        }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::BindVertex>) { glBindVertexBuffer(0,m_impl->buffers.at(c.handle).id,0,static_cast<GLsizei>(c.stride)); }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::BindIndex>) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m_impl->buffers.at(c.handle).id); }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::BindTextureCmd>) { glBindTextureUnit(c.slot,m_impl->images.at(c.handle)); }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::DrawCmd>) { glDrawArraysInstancedBaseInstance(ToGLTopology(c.topology),static_cast<GLint>(c.first),static_cast<GLsizei>(c.count),static_cast<GLsizei>(c.instances),c.firstInstance); }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::DrawIndexedCmd>) { glDrawElements(GL_TRIANGLES,static_cast<GLsizei>(c.count),GL_UNSIGNED_INT,nullptr); }
        else if constexpr (std::is_same_v<T, OpenGLCommandList::DispatchCmd>) { glDispatchCompute(c.x,c.y,c.z); }
    }, command);
    if (eglSwapBuffers(m_impl->group->display, target->surface) != EGL_TRUE)
        throw std::runtime_error("EGL failed to present the rendered frame");
}

} // namespace Lighthouse
