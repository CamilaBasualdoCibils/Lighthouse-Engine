#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include "OpenGLDeviceGroup.hpp"
#include "OpenGLCommandList.hpp"
#include "OpenGLRenderDeviceImpl.hpp"
#include "OpenGLShaderTranslator.hpp"

#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <EGL/egl.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#ifdef LIGHTHOUSE_IMGUI
#include <imgui_impl_opengl3.h>
#endif

namespace Lighthouse {
namespace {

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

bool HasOpenGLSpirvSupport() {
    const auto* versionText = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (!versionText)
        return false;
    GLint major{}, minor{};
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    bool supported = major > 4 || (major == 4 && minor >= 6);
    GLint extensionCount{};
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint i = 0; !supported && i < extensionCount; ++i) {
        const auto* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        supported = extension && std::string_view(extension) == "GL_ARB_gl_spirv";
    }
    if (!supported)
        return false;
    GLint formatCount{};
    glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &formatCount);
    std::vector<GLint> formats(static_cast<std::size_t>(std::max(formatCount, 0)));
    if (!formats.empty())
        glGetIntegerv(GL_SHADER_BINARY_FORMATS, formats.data());
    return std::find(formats.begin(), formats.end(), GL_SHADER_BINARY_FORMAT_SPIR_V) != formats.end();
}

void CompileGLSL(GLuint shader, const std::string& source) {
    const auto* data = source.c_str();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &data, &length);
    glCompileShader(shader);
    GLint ok{};
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logSize{};
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
        std::string log(static_cast<std::size_t>(std::max(logSize, 1)), '\0');
        glGetShaderInfoLog(shader, logSize, nullptr, log.data());
        throw std::runtime_error("OpenGL shader compilation failed: " + log);
    }
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

template<class HandleType>
HandleType MakeHandle(std::uint32_t index, std::uint64_t owner) { return HandleType{index, 1, owner}; }


} // namespace



ImageViewHandle OpenGLRenderDevice::AcquireSwapchainImage(SwapchainHandle swapchain) {
    const auto* target = m_impl->FindSwapchain(swapchain);
    if (!target) throw std::invalid_argument("OpenGL acquire received an invalid swapchain handle");
    if (m_impl->acquired) throw std::logic_error("An OpenGL swapchain image is already acquired");
    m_impl->ActivateContext(target);
    m_impl->acquired = swapchain;
    m_impl->frameReady = false;
    return {0x80000000u | swapchain.index, swapchain.generation, DeviceId()};
}


OpenGLRenderDevice::OpenGLRenderDevice(std::shared_ptr<Detail::OpenGLDeviceGroup> group,
                                       const DeviceConfig&)
    : m_impl(std::make_unique<Impl>(std::move(group), DeviceId())) {}
OpenGLRenderDevice::~OpenGLRenderDevice() {
    if (!m_impl || !m_impl->group || m_impl->group->display == EGL_NO_DISPLAY) return;
    m_impl->ActivateContext();
    for (const auto& [_, p] : m_impl->pipelines) {
        if (const auto vao = p.vaos.find(SwapchainHandle{}); vao != p.vaos.end())
            glDeleteVertexArrays(1, &vao->second);
        glDeleteProgram(p.program);
    }
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
    if (m_impl->acquired == handle) { m_impl->acquired.reset(); m_impl->frameReady = false; }
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
    try {
        GLint uniformBuffers{}, storageBuffers{}, textureUnits{}, imageUnits{}, atomicCounterBuffers{};
        glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &uniformBuffers);
        glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &storageBuffers);
        glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textureUnits);
        glGetIntegerv(GL_MAX_IMAGE_UNITS, &imageUnits);
        glGetIntegerv(GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, &atomicCounterBuffers);
        if (desc.bytecode.empty() || desc.bytecode.size_bytes() % sizeof(std::uint32_t))
            throw std::invalid_argument("Shader module input must contain SPIR-V words");
        const auto words = std::span<const std::uint32_t>(
            reinterpret_cast<const std::uint32_t*>(desc.bytecode.data()),
            desc.bytecode.size_bytes() / sizeof(std::uint32_t));
        const auto glsl = Detail::TranslateShaderToOpenGL(words, desc.stage, desc.entryPoint,
            {static_cast<std::uint32_t>(std::max(uniformBuffers, 0)),
             static_cast<std::uint32_t>(std::max(storageBuffers, 0)),
             static_cast<std::uint32_t>(std::max(textureUnits, 0)),
             static_cast<std::uint32_t>(std::max(imageUnits, 0)),
             static_cast<std::uint32_t>(std::max(atomicCounterBuffers, 0))});
        if (HasOpenGLSpirvSupport()) {
            const auto spirv = ShaderCompiler::CompileOpenGL(glsl, desc.stage);
            auto specialize = reinterpret_cast<PFNGLSPECIALIZESHADERPROC>(eglGetProcAddress("glSpecializeShader"));
            if (!specialize)
                specialize = reinterpret_cast<PFNGLSPECIALIZESHADERPROC>(eglGetProcAddress("glSpecializeShaderARB"));
            if (!specialize)
                throw std::runtime_error("OpenGL reports SPIR-V support but glSpecializeShader is unavailable");
            glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, spirv.data(),
                           static_cast<GLsizei>(spirv.size() * sizeof(std::uint32_t)));
            const std::string entryPoint{"main"};
            specialize(shader, entryPoint.c_str(), 0, nullptr, nullptr);
            GLint specialized{};
            glGetShaderiv(shader, GL_COMPILE_STATUS, &specialized);
            if (!specialized) {
                GLint logSize{};
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
                std::string log(static_cast<std::size_t>(std::max(logSize, 1)), '\0');
                glGetShaderInfoLog(shader, logSize, nullptr, log.data());
                throw std::runtime_error("OpenGL SPIR-V specialization failed: " + log);
            }
        } else {
            CompileGLSL(shader, glsl);
        }
    } catch (...) {
        glDeleteShader(shader);
        throw;
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

ImageViewHandle OpenGLRenderDevice::AcquiredImageView() const noexcept {
    if (!m_impl->acquired) return {};
    const auto swapchain = *m_impl->acquired;
    return {0x80000000u | swapchain.index, swapchain.generation, DeviceId()};
}

void OpenGLRenderDevice::Submit(const IRenderCommandList& list) {
    const auto* commands = dynamic_cast<const OpenGLCommandList*>(&list);
    if (!commands) throw std::invalid_argument("OpenGL submit received a command list from another backend");
    if (m_impl->acquired) m_impl->ActivateContext(m_impl->FindSwapchain(*m_impl->acquired));
    else m_impl->ActivateContext();
    try {
        for (const auto& command : commands->Commands())
            std::visit([this](const auto& value) { value.Execute(this); }, command);
    } catch (...) {
        m_impl->frameReady = false;
        throw;
    }
}

void OpenGLRenderDevice::Present(SwapchainHandle swapchain) {
    const auto* target = m_impl->FindSwapchain(swapchain);
    if (!target || m_impl->acquired != swapchain || !m_impl->frameReady)
        throw std::logic_error("OpenGL present requires the acquired swapchain image");
    m_impl->ActivateContext(target);
    if (eglSwapBuffers(m_impl->group->display, target->surface) != EGL_TRUE)
        throw std::runtime_error("EGL failed to present the rendered frame");
    m_impl->acquired.reset();
    m_impl->frameReady = false;
}

#ifdef LIGHTHOUSE_IMGUI
void OpenGLRenderDevice::InitializeImGui(SwapchainHandle swapchain) {
    const auto* target = m_impl->FindSwapchain(swapchain);
    if (!target) throw std::invalid_argument("ImGui requires a valid OpenGL swapchain");
    m_impl->ActivateContext(target);
    if (!ImGui_ImplOpenGL3_Init("#version 450 core"))
        throw std::runtime_error("Failed to initialize the ImGui OpenGL renderer");
}

void OpenGLRenderDevice::ShutdownImGui() noexcept {
    try {
        if (m_impl->acquired) m_impl->ActivateContext(m_impl->FindSwapchain(*m_impl->acquired));
        else m_impl->ActivateContext();
        ImGui_ImplOpenGL3_Shutdown();
    } catch (...) {}
}

void OpenGLRenderDevice::NewImGuiFrame() { ImGui_ImplOpenGL3_NewFrame(); }

void OpenGLRenderDevice::RenderImGui(SwapchainHandle swapchain, ImageViewHandle image, ImDrawData* drawData) {
    if (m_impl->acquired != swapchain || image != AcquiredImageView())
        throw std::logic_error("ImGui must render to the acquired OpenGL image");
    m_impl->ActivateContext(m_impl->FindSwapchain(swapchain));
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
    m_impl->frameReady = true;
}
#endif

} // namespace Lighthouse
