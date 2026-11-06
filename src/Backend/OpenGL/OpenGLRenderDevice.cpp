#include "OpenGLCommandList.hpp"
#include "OpenGLDeviceGroup.hpp"
#include "OpenGLRenderDeviceImpl.hpp"
#include "OpenGLShaderTranslator.hpp"
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Engine/Rendering/Detail/ImGuiGlfwRouting.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#ifdef LIGHTHOUSE_IMGUI
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#undef None
#endif

namespace Lighthouse
{
namespace
{

GLenum ToGLShaderStage(ShaderStageFlags stage)
{
  switch (stage)
  {
  case ShaderStageFlags::Vertex:
    return GL_VERTEX_SHADER;
  case ShaderStageFlags::Fragment:
    return GL_FRAGMENT_SHADER;
  case ShaderStageFlags::Compute:
    return GL_COMPUTE_SHADER;
  }
  throw std::invalid_argument("Unsupported shader stage");
}

GLenum ToGLImageFormat(ImageFormat format)
{
  switch (format)
  {
  case ImageFormat::R8:
    return GL_R8;
  case ImageFormat::RG8:
    return GL_RG8;
  case ImageFormat::RGBA8:
  case ImageFormat::BGRA8:
    return GL_RGBA8;
  case ImageFormat::RGBA8Srgb:
  case ImageFormat::BGRA8Srgb:
    return GL_SRGB8_ALPHA8;
  case ImageFormat::R16f:
    return GL_R16F;
  case ImageFormat::RG16f:
    return GL_RG16F;
  case ImageFormat::RGB16f:
    return GL_RGB16F;
  case ImageFormat::RGBA16f:
    return GL_RGBA16F;
  case ImageFormat::R32f:
    return GL_R32F;
  case ImageFormat::RG32f:
    return GL_RG32F;
  case ImageFormat::RGB32f:
    return GL_RGB32F;
  case ImageFormat::RGBA32f:
    return GL_RGBA32F;
  case ImageFormat::RGB8:
    return GL_RGB8;
  case ImageFormat::Depth16:
    return GL_DEPTH_COMPONENT16;
  case ImageFormat::Depth24Stencil8:
    return GL_DEPTH24_STENCIL8;
  case ImageFormat::Depth32:
    return GL_DEPTH_COMPONENT32F;
  case ImageFormat::Undefined:
    break;
  }
  throw std::invalid_argument("Unsupported image format");
}

bool HasOpenGLSpirvSupport()
{
  const auto* versionText =
      reinterpret_cast<const char*>(glGetString(GL_VERSION));
  if (!versionText)
    return false;
  GLint major{}, minor{};
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  bool supported = major > 4 || (major == 4 && minor >= 6);
  GLint extensionCount{};
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
  for (GLint i = 0; !supported && i < extensionCount; ++i)
  {
    const auto* extension = reinterpret_cast<const char*>(
        glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
    supported = extension && std::string_view(extension) == "GL_ARB_gl_spirv";
  }
  if (!supported)
    return false;
  GLint formatCount{};
  glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &formatCount);
  std::vector<GLint> formats(
      static_cast<std::size_t>(std::max(formatCount, 0)));
  if (!formats.empty())
    glGetIntegerv(GL_SHADER_BINARY_FORMATS, formats.data());
  return std::find(formats.begin(), formats.end(),
                   GL_SHADER_BINARY_FORMAT_SPIR_V) != formats.end();
}

void CompileGLSL(GLuint shader, const std::string& source)
{
  const auto* data = source.c_str();
  const auto length = static_cast<GLint>(source.size());
  glShaderSource(shader, 1, &data, &length);
  glCompileShader(shader);
  GLint ok{};
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    GLint logSize{};
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
    std::string log(static_cast<std::size_t>(std::max(logSize, 1)), '\0');
    glGetShaderInfoLog(shader, logSize, nullptr, log.data());
    throw std::runtime_error("OpenGL shader compilation failed: " + log);
  }
}

GLenum ToGLImageType(ImageType type)
{
  switch (type)
  {
  case ImageType::Image2D:
    return GL_TEXTURE_2D;
  case ImageType::Image1D:
    return GL_TEXTURE_1D;
  case ImageType::Image3D:
    return GL_TEXTURE_3D;
  case ImageType::Image2DArray:
    return GL_TEXTURE_2D_ARRAY;
  case ImageType::Cube:
    return GL_TEXTURE_CUBE_MAP;
  case ImageType::CubeArray:
    return GL_TEXTURE_CUBE_MAP_ARRAY;
  }
  throw std::invalid_argument("Unsupported image type");
}

template <class HandleType>
HandleType MakeHandle(std::uint32_t index, std::uint64_t owner)
{
  return HandleType{index, 1, owner};
}

} // namespace

void OpenGLRenderDevice::Impl::DestroySwapchainImages(Swapchain& swapchain)
{
  ActivateContext(&swapchain);
  for (const auto& image : swapchain.images)
  {
    glDeleteFramebuffers(1, &image.framebuffer);
    if (const auto view = views.find(image.view); view != views.end())
    {
      glDeleteTextures(1, &view->second.id);
      views.erase(view);
    }
    if (const auto texture = images.find(image.image); texture != images.end())
    {
      glDeleteTextures(1, &texture->second.id);
      images.erase(texture);
    }
    for (auto& [_, set] : sets)
      for (auto& [__, values] : set.resources)
        for (auto& value : values)
          if (auto info = std::get_if<DescriptorImageInfo>(&value);
              info && info->image == image.view)
            value = std::monostate{};
  }
  swapchain.images.clear();
}

void OpenGLRenderDevice::Impl::CreateSwapchainImages(Swapchain& swapchain,
                                                     std::uint32_t count)
{
  ActivateContext(&swapchain);
  EGLint width{}, height{};
  if (!eglQuerySurface(group->display, swapchain.surface, EGL_WIDTH, &width) ||
      !eglQuerySurface(group->display, swapchain.surface, EGL_HEIGHT, &height))
    throw std::runtime_error("Failed to query the OpenGL swapchain extent");
  swapchain.width = std::max(width, 1);
  swapchain.height = std::max(height, 1);
  swapchain.images.reserve(count);
  try
  {
    for (std::uint32_t i = 0; i < count; ++i)
    {
      auto& image = swapchain.images.emplace_back();
      GLuint texture{}, view{};
      glCreateTextures(GL_TEXTURE_2D, 1, &texture);
      image.image = MakeHandle<ImageHandle>(nextImage++, owner);
      images.emplace(
          image.image,
          ImageResource{texture, ImageUsage::ColorAttachment |
                                     ImageUsage::TransferSource |
                                     ImageUsage::TransferDestination |
                                     ImageUsage::Sampled});
      glTextureStorage2D(texture, 1, GL_RGBA8, swapchain.width,
                         swapchain.height);
      glGenTextures(1, &view);
      image.view = MakeHandle<ImageViewHandle>(nextView++, owner);
      views.emplace(image.view, ImageViewResource{view, image.image});
      glTextureView(view, GL_TEXTURE_2D, texture, GL_RGBA8, 0, 1, 0, 1);
      glCreateFramebuffers(1, &image.framebuffer);
      glNamedFramebufferTexture(image.framebuffer, GL_COLOR_ATTACHMENT0, view,
                                0);
      glNamedFramebufferReadBuffer(image.framebuffer, GL_COLOR_ATTACHMENT0);
      constexpr GLenum attachment = GL_COLOR_ATTACHMENT0;
      glNamedFramebufferDrawBuffers(image.framebuffer, 1, &attachment);
      if (glCheckNamedFramebufferStatus(image.framebuffer, GL_FRAMEBUFFER) !=
          GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error(
            "OpenGL swapchain image framebuffer is incomplete");
    }
  }
  catch (...)
  {
    DestroySwapchainImages(swapchain);
    throw;
  }
  swapchain.imageIndex = 0;
}

AcquiredSwapchainImage
OpenGLRenderDevice::AcquireSwapchainImage(SwapchainHandle swapchain)
{
  auto* target = m_impl->FindSwapchain(swapchain);
  if (!target)
    throw std::invalid_argument(
        "OpenGL acquire received an invalid swapchain handle");
  if (target->acquired)
    throw std::logic_error("An OpenGL swapchain image is already acquired");
  m_impl->ActivateContext(target);
  // Keep the actual images in step with the native window's framebuffer size.
  EGLint width{}, height{};
  if (!eglQuerySurface(m_impl->group->display, target->surface, EGL_WIDTH,
                       &width) ||
      !eglQuerySurface(m_impl->group->display, target->surface, EGL_HEIGHT,
                       &height))
    throw std::runtime_error("Failed to query the OpenGL swapchain extent");
  if (target->width != std::max(width, 1) ||
      target->height != std::max(height, 1))
  {
    const auto count = static_cast<std::uint32_t>(target->images.size());
    // Build the replacement first so allocation failure leaves the old images
    // intact.
    Impl::Swapchain replacement;
    replacement.surface = target->surface;
    replacement.context = target->context;
    m_impl->CreateSwapchainImages(replacement, count);
    m_impl->DestroySwapchainImages(*target);
    target->images = std::move(replacement.images);
    target->width = replacement.width;
    target->height = replacement.height;
    target->imageIndex = 0;
  }
  m_impl->activeSwapchain = swapchain;
  target->acquired = true;
  target->frameReady = false;
  const auto& image = target->images.at(target->imageIndex);
  return {.image = image.image, .view = image.view};
}

OpenGLRenderDevice::OpenGLRenderDevice(
    std::shared_ptr<Detail::OpenGLDeviceGroup> group, const DeviceConfig&)
    : m_impl(std::make_unique<Impl>(std::move(group), DeviceId()))
{
  m_impl->group->Initialize();
  m_impl->ActivateContext();
}
OpenGLRenderDevice::~OpenGLRenderDevice()
{
  if (!m_impl || !m_impl->group || m_impl->group->display == EGL_NO_DISPLAY)
    return;
  m_impl->ActivateContext();
  for (const auto& [_, p] : m_impl->pipelines)
  {
    if (const auto vao = p.vaos.find(SwapchainHandle{}); vao != p.vaos.end())
      glDeleteVertexArrays(1, &vao->second);
    glDeleteProgram(p.program);
  }
  for (std::uint32_t i = 0; i < m_impl->swapchains.size(); ++i)
  {
    auto& slot = m_impl->swapchains[i];
    if (!slot.value)
      continue;
    m_impl->ActivateContext(slot.value.get());
    m_impl->DestroySwapchainImages(*slot.value);
    const SwapchainHandle handle{i, slot.generation, DeviceId()};
    for (auto& [_, pipeline] : m_impl->pipelines)
    {
      if (const auto vao = pipeline.vaos.find(handle);
          vao != pipeline.vaos.end())
      {
        glDeleteVertexArrays(1, &vao->second);
        pipeline.vaos.erase(vao);
      }
    }
  }
  m_impl->ActivateContext();
  for (const auto& [_, s] : m_impl->shaders)
    (void)s;
  for (const auto& [_, sampler] : m_impl->samplers)
    glDeleteSamplers(1, &sampler);
  for (const auto& [_, b] : m_impl->buffers)
    glDeleteBuffers(1, &b.id);
  for (const auto& [_, v] : m_impl->views)
    glDeleteTextures(1, &v.id);
  for (const auto& [_, i] : m_impl->images)
    glDeleteTextures(1, &i.id);
  for (auto& slot : m_impl->swapchains)
    if (slot.value)
    {
      if (eglGetCurrentContext() == slot.value->context)
        eglMakeCurrent(m_impl->group->display, m_impl->group->pbuffer,
                       m_impl->group->pbuffer, m_impl->group->shareContext);
      eglDestroyContext(m_impl->group->display, slot.value->context);
      eglDestroySurface(m_impl->group->display, slot.value->surface);
      slot.value.reset();
    }
}

SwapchainHandle
OpenGLRenderDevice::CreateSwapchain(Window& window,
                                    const SwapchainConfig& config)
{
  if (config.minImageCount == 0)
    throw std::invalid_argument("Swapchain minImageCount must be at least one");
  if (!window.NativeDisplayHandle() || !window.NativeEGLWindowHandle())
    throw std::invalid_argument(
        "OpenGL swapchain requires an EGL-enabled window from WindowManager");
  m_impl->group->Initialize(window.NativeDisplayHandle());
  if (m_impl->group->nativeDisplay != window.NativeDisplayHandle())
    throw std::invalid_argument(
        "All swapchains on an OpenGL device must use the same EGL display");
  auto state = std::make_unique<Impl::Swapchain>();
  state->surface =
      m_impl->group->CreateWindowSurface(window.NativeEGLWindowHandle());
  try
  {
    state->context = m_impl->group->CreateSharedContext();
    m_impl->ActivateContext(state.get());
    if (eglSwapInterval(m_impl->group->display, config.vsync ? 1 : 0) !=
        EGL_TRUE)
      throw std::runtime_error("Failed to configure EGL swap interval");
    m_impl->CreateSwapchainImages(*state, config.minImageCount);
  }
  catch (...)
  {
    if (state->context != EGL_NO_CONTEXT)
    {
      eglMakeCurrent(m_impl->group->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
      eglDestroyContext(m_impl->group->display, state->context);
    }
    eglDestroySurface(m_impl->group->display, state->surface);
    throw;
  }
  std::uint32_t index = 0;
  while (index < m_impl->swapchains.size() && m_impl->swapchains[index].value)
    ++index;
  if (index == m_impl->swapchains.size())
    m_impl->swapchains.emplace_back();
  auto& slot = m_impl->swapchains[index];
  slot.value = std::move(state);
  return {index, slot.generation, DeviceId()};
}

bool OpenGLRenderDevice::DestroySwapchain(SwapchainHandle handle)
{
  if (!handle || handle.owner != DeviceId() ||
      handle.index >= m_impl->swapchains.size())
    return false;
  auto& slot = m_impl->swapchains[handle.index];
  if (!slot.value || slot.generation != handle.generation)
    return false;
  if (m_impl->activeSwapchain == handle)
    m_impl->activeSwapchain.reset();
  m_impl->ActivateContext(slot.value.get());
  m_impl->DestroySwapchainImages(*slot.value);
  for (auto& [_, pipeline] : m_impl->pipelines)
  {
    if (const auto vao = pipeline.vaos.find(handle); vao != pipeline.vaos.end())
    {
      glDeleteVertexArrays(1, &vao->second);
      pipeline.vaos.erase(vao);
    }
  }
  if (eglGetCurrentContext() == slot.value->context)
    eglMakeCurrent(m_impl->group->display, m_impl->group->pbuffer,
                   m_impl->group->pbuffer, m_impl->group->shareContext);
  eglDestroyContext(m_impl->group->display, slot.value->context);
  eglDestroySurface(m_impl->group->display, slot.value->surface);
  slot.value.reset();
  if (++slot.generation == 0)
    ++slot.generation;
  return true;
}
bool OpenGLRenderDevice::IsValid(SwapchainHandle handle) const noexcept
{
  return m_impl->FindSwapchain(handle) != nullptr;
}

ImageHandle OpenGLRenderDevice::CreateImage(const ImageDesc& desc)
{
  m_impl->ActivateContext();
  const auto target = ToGLImageType(desc.type);
  const auto format = ToGLImageFormat(desc.format);
  GLuint texture{};
  glCreateTextures(target, 1, &texture);
  if (desc.type == ImageType::Image2D)
    glTextureStorage2D(texture, static_cast<GLsizei>(desc.mipLevels), format,
                       static_cast<GLsizei>(desc.width),
                       static_cast<GLsizei>(desc.height));
  else if (desc.type == ImageType::Image3D ||
           desc.type == ImageType::Image2DArray ||
           desc.type == ImageType::CubeArray)
    glTextureStorage3D(texture, static_cast<GLsizei>(desc.mipLevels), format,
                       static_cast<GLsizei>(desc.width),
                       static_cast<GLsizei>(desc.height),
                       static_cast<GLsizei>(desc.depth * desc.arrayLayers));
  else if (desc.type == ImageType::Image1D)
    glTextureStorage1D(texture, static_cast<GLsizei>(desc.mipLevels), format,
                       static_cast<GLsizei>(desc.width));
  else
    glTextureStorage2D(texture, static_cast<GLsizei>(desc.mipLevels), format,
                       static_cast<GLsizei>(desc.width),
                       static_cast<GLsizei>(desc.height));
  if (!desc.initialData.empty() && desc.type == ImageType::Image2D)
    glTextureSubImage2D(texture, 0, 0, 0, static_cast<GLsizei>(desc.width),
                        static_cast<GLsizei>(desc.height), GL_RGBA,
                        GL_UNSIGNED_BYTE, desc.initialData.data());
  const auto handle = MakeHandle<ImageHandle>(m_impl->nextImage++, DeviceId());
  m_impl->images.emplace(
      handle, Impl::ImageResource{texture, desc.usage, desc.type, desc.format});
  return handle;
}
std::vector<std::byte> OpenGLRenderDevice::ReadImage(ImageHandle handle)
{
  WaitIdle();
  m_impl->ActivateContext();
  const auto& image = m_impl->images.at(handle);
  if (image.type != ImageType::Image2D)
    throw std::invalid_argument("ReadImage expects a 2D image");
  GLint width{}, height{};
  glGetTextureLevelParameteriv(image.id, 0, GL_TEXTURE_WIDTH, &width);
  glGetTextureLevelParameteriv(image.id, 0, GL_TEXTURE_HEIGHT, &height);
  std::vector<std::byte> bytes(static_cast<std::size_t>(width) * height * 4);
  glGetTextureImage(image.id, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    static_cast<GLsizei>(bytes.size()), bytes.data());
  return bytes;
}
ImageViewHandle OpenGLRenderDevice::CreateImageView(const ImageViewDesc& desc)
{
  m_impl->ActivateContext();
  const auto source = m_impl->images.at(desc.image).id;
  GLuint view{};
  glGenTextures(1, &view);
  glTextureView(view, ToGLImageType(desc.type), source,
                ToGLImageFormat(desc.format), desc.baseMipLevel,
                desc.mipLevelCount, desc.baseArrayLayer, desc.arrayLayerCount);
  const auto handle =
      MakeHandle<ImageViewHandle>(m_impl->nextView++, DeviceId());
  m_impl->views.emplace(handle, Impl::ImageViewResource{view, desc.image});
  return handle;
}

void OpenGLRenderDevice::UpdateBuffer(BufferHandle handle, std::uint64_t offset,
                                      std::span<const std::byte> bytes)
{
  if (!IsValid(handle))
    throw std::invalid_argument("Invalid buffer for update");
  auto& buffer = m_impl->buffers.at(handle);
  if (offset > buffer.metadata.size ||
      bytes.size() > buffer.metadata.size - offset)
    throw std::out_of_range("Buffer update exceeds allocation");
  WaitIdle();
  m_impl->ActivateContext();
  glNamedBufferSubData(buffer.id, static_cast<GLintptr>(offset),
                       static_cast<GLsizeiptr>(bytes.size()), bytes.data());
}

BufferHandle OpenGLRenderDevice::CreateBuffer(const BufferDesc& desc)
{
  m_impl->ActivateContext();
  if (desc.initialData.size_bytes() > desc.size)
    throw std::invalid_argument("Buffer initial data exceeds buffer size");
  GLuint id{};
  glCreateBuffers(1, &id);
  glNamedBufferStorage(id, static_cast<GLsizeiptr>(desc.size), nullptr,
                       GL_DYNAMIC_STORAGE_BIT);
  if (!desc.initialData.empty())
    glNamedBufferSubData(id, 0,
                         static_cast<GLsizeiptr>(desc.initialData.size()),
                         desc.initialData.data());
  const auto handle =
      MakeHandle<BufferHandle>(m_impl->nextBuffer++, DeviceId());
  m_impl->buffers.emplace(
      handle, Impl::BufferResource{
                  id, BufferMetadata{desc.size, desc.usage, desc.memory}});
  return handle;
}
std::optional<BufferMetadata>
OpenGLRenderDevice::GetBufferDesc(BufferHandle handle) const noexcept
{
  const auto it = m_impl->buffers.find(handle);
  if (it == m_impl->buffers.end())
    return std::nullopt;
  return it->second.metadata;
}
bool OpenGLRenderDevice::IsValid(BufferHandle handle) const noexcept
{
  return m_impl->buffers.contains(handle);
}
bool OpenGLRenderDevice::IsValid(
    DescriptorSetLayoutHandle handle) const noexcept
{
  return m_impl->setLayouts.contains(handle);
}
bool OpenGLRenderDevice::IsValid(DescriptorSetHandle handle) const noexcept
{
  return m_impl->sets.contains(handle);
}
bool OpenGLRenderDevice::IsValid(PipelineLayoutHandle handle) const noexcept
{
  return m_impl->pipelineLayouts.contains(handle);
}
bool OpenGLRenderDevice::IsValid(SamplerHandle handle) const noexcept
{
  return m_impl->samplers.contains(handle);
}

DescriptorSetLayoutHandle OpenGLRenderDevice::CreateDescriptorSetLayout(
    const DescriptorSetLayoutDesc& desc)
{
  Impl::DescriptorSetLayout layout;
  constexpr auto validStages =
      static_cast<std::uint32_t>(ShaderStageFlags::Vertex) |
      static_cast<std::uint32_t>(ShaderStageFlags::Fragment) |
      static_cast<std::uint32_t>(ShaderStageFlags::Compute);
  for (const auto& binding : desc.bindings)
  {
    const auto stages = static_cast<std::uint32_t>(binding.stages);
    if (!binding.count || !stages || (stages & ~validStages))
      throw std::invalid_argument(
          "Descriptor binding has invalid count or shader stages");
    if (binding.type != DescriptorType::UniformBuffer &&
        binding.type != DescriptorType::StorageBuffer &&
        binding.type != DescriptorType::CombinedImageSampler)
      throw std::invalid_argument("Unsupported descriptor type");
    if (!layout.bindings.emplace(binding.binding, binding).second)
      throw std::invalid_argument("Duplicate descriptor binding");
  }
  const auto handle = MakeHandle<DescriptorSetLayoutHandle>(
      m_impl->nextSetLayout++, DeviceId());
  m_impl->setLayouts.emplace(handle, std::move(layout));
  return handle;
}

DescriptorSetHandle
OpenGLRenderDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
{
  const auto layout = m_impl->setLayouts.find(desc.layout);
  if (layout == m_impl->setLayouts.end())
    throw std::invalid_argument(
        "Descriptor set layout is invalid or belongs to another device");
  Impl::DescriptorSet set;
  set.layout = desc.layout;
  for (const auto& [binding, info] : layout->second.bindings)
    set.resources.emplace(binding,
                          std::vector<Impl::StoredResource>(info.count));
  const auto handle =
      MakeHandle<DescriptorSetHandle>(m_impl->nextSet++, DeviceId());
  m_impl->sets.emplace(handle, std::move(set));
  return handle;
}

PipelineLayoutHandle
OpenGLRenderDevice::CreatePipelineLayout(const PipelineLayoutDesc& desc)
{
  Impl::PipelineLayout layout;
  layout.sets = desc.sets;
  std::uint32_t ubo{}, ssbo{}, texture{};
  for (std::uint32_t setIndex = 0; setIndex < desc.sets.size(); ++setIndex)
  {
    const auto source = m_impl->setLayouts.find(desc.sets[setIndex]);
    if (source == m_impl->setLayouts.end())
      throw std::invalid_argument(
          "Pipeline layout references an invalid descriptor set layout");
    for (const auto& [binding, info] : source->second.bindings)
    {
      auto* next = info.type == DescriptorType::UniformBuffer   ? &ubo
                   : info.type == DescriptorType::StorageBuffer ? &ssbo
                                                                : &texture;
      layout.locations.emplace(
          std::pair{setIndex, binding},
          PipelineLayoutLocation{info.type, *next, info.count, info.stages});
      if (info.count > std::numeric_limits<std::uint32_t>::max() - *next)
        throw std::overflow_error("Descriptor binding point overflow");
      *next += info.count;
    }
  }
  GLint maxUbo{}, maxSsbo{}, maxTexture{};
  // Limits are queried once a GL context exists; layouts may be built before a
  // swapchain, so pipeline validation repeats these checks.
  if (m_impl->group && m_impl->group->display != EGL_NO_DISPLAY)
  {
    m_impl->ActivateContext();
    glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &maxUbo);
    glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxSsbo);
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxTexture);
    if (ubo > static_cast<std::uint32_t>(std::max(maxUbo, 0)) ||
        ssbo > static_cast<std::uint32_t>(std::max(maxSsbo, 0)) ||
        texture > static_cast<std::uint32_t>(std::max(maxTexture, 0)))
      throw std::runtime_error(
          "Pipeline layout exceeds OpenGL descriptor binding limits");
  }
  const auto handle = MakeHandle<PipelineLayoutHandle>(
      m_impl->nextPipelineLayout++, DeviceId());
  m_impl->pipelineLayouts.emplace(handle, std::move(layout));
  return handle;
}

SamplerHandle OpenGLRenderDevice::CreateSampler(const SamplerDesc& desc)
{
  m_impl->ActivateContext();
  if (desc.anisotropy &&
      (!std::isfinite(desc.maxAnisotropy) || desc.maxAnisotropy < 1.f))
    throw std::invalid_argument(
        "Sampler anisotropy must be finite and at least one");
  auto filter = [](SamplerFilter f)
  {
    return f == SamplerFilter::Nearest ? GL_NEAREST
           : f == SamplerFilter::Linear
               ? GL_LINEAR
               : throw std::invalid_argument("Invalid sampler filter");
  };
  auto address = [](SamplerAddressMode mode)
  {
    switch (mode)
    {
    case SamplerAddressMode::Repeat:
      return GL_REPEAT;
    case SamplerAddressMode::MirroredRepeat:
      return GL_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToEdge:
      return GL_CLAMP_TO_EDGE;
    case SamplerAddressMode::ClampToBorder:
      return GL_CLAMP_TO_BORDER;
    }
    throw std::invalid_argument("Invalid sampler address mode");
  };
  const auto minFilter =
      desc.mipmap == SamplerMipmapFilter::None ? filter(desc.minification)
      : desc.mipmap == SamplerMipmapFilter::Nearest
          ? (desc.minification == SamplerFilter::Nearest
                 ? GL_NEAREST_MIPMAP_NEAREST
                 : GL_LINEAR_MIPMAP_NEAREST)
      : desc.mipmap == SamplerMipmapFilter::Linear
          ? (desc.minification == SamplerFilter::Nearest
                 ? GL_NEAREST_MIPMAP_LINEAR
                 : GL_LINEAR_MIPMAP_LINEAR)
          : throw std::invalid_argument("Invalid mipmap filter");
  GLuint sampler{};
  glCreateSamplers(1, &sampler);
  glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, minFilter);
  glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER,
                      filter(desc.magnification));
  glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, address(desc.addressU));
  glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, address(desc.addressV));
  glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, address(desc.addressW));
  if (desc.anisotropy)
  {
    bool supported{};
    GLint extensionCount{};
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint index = 0; index < extensionCount; ++index)
    {
      const auto* ext = reinterpret_cast<const char*>(
          glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
      if (ext && std::string_view(ext) == "GL_EXT_texture_filter_anisotropic")
      {
        supported = true;
        break;
      }
    }
    if (!supported)
    {
      glDeleteSamplers(1, &sampler);
      throw std::runtime_error(
          "Anisotropic filtering is not supported by this OpenGL device");
    }
    GLfloat maximum{};
    glGetFloatv(0x84FF, &maximum);
    glSamplerParameterf(sampler, 0x84FE, std::min(desc.maxAnisotropy, maximum));
  }
  const auto handle =
      MakeHandle<SamplerHandle>(m_impl->nextSampler++, DeviceId());
  m_impl->samplers.emplace(handle, sampler);
  return handle;
}

void OpenGLRenderDevice::UpdateDescriptorSet(
    DescriptorSetHandle handle, std::span<const DescriptorWrite> writes)
{
  m_impl->ActivateContext();
  auto found = m_impl->sets.find(handle);
  if (found == m_impl->sets.end())
    throw std::invalid_argument("Invalid descriptor set");
  auto updated = found->second.resources;
  const auto layout = m_impl->setLayouts.find(found->second.layout);
  if (layout == m_impl->setLayouts.end())
    throw std::invalid_argument("Descriptor set layout has been destroyed");
  for (const auto& write : writes)
  {
    const auto binding = layout->second.bindings.find(write.binding);
    if (binding == layout->second.bindings.end())
      throw std::invalid_argument("Descriptor write binding does not exist");
    if (write.arrayElement >= binding->second.count)
      throw std::out_of_range("Descriptor write array element is out of range");
    auto& target = updated.at(write.binding).at(write.arrayElement);
    if (binding->second.type == DescriptorType::UniformBuffer ||
        binding->second.type == DescriptorType::StorageBuffer)
    {
      if (!std::holds_alternative<DescriptorBufferInfo>(write.resource))
        throw std::invalid_argument(
            "Descriptor resource type does not match buffer binding");
      auto info = std::get<DescriptorBufferInfo>(write.resource);
      const auto buffer = m_impl->buffers.find(info.buffer);
      if (buffer == m_impl->buffers.end() || info.buffer.owner != DeviceId())
        throw std::invalid_argument(
            "Invalid or cross-device descriptor buffer");
      const auto required =
          binding->second.type == DescriptorType::UniformBuffer
              ? BufferUsage::Uniform
              : BufferUsage::Storage;
      if (!buffer->second.metadata.usage.Contains(required))
        throw std::invalid_argument("Descriptor buffer has incompatible usage");
      if (info.offset >= buffer->second.metadata.size)
        throw std::out_of_range("Descriptor buffer offset is out of range");
      if (info.range == WholeBuffer)
        info.range = buffer->second.metadata.size - info.offset;
      if (!info.range ||
          info.range > buffer->second.metadata.size - info.offset)
        throw std::out_of_range("Descriptor buffer range is out of bounds");
      GLint alignment{};
      glGetIntegerv(binding->second.type == DescriptorType::UniformBuffer
                        ? GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT
                        : GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,
                    &alignment);
      if (alignment > 0 &&
          info.offset % static_cast<std::uint64_t>(alignment) != 0)
        throw std::invalid_argument("Descriptor buffer offset does not meet "
                                    "OpenGL alignment requirements");
      target = info;
    }
    else
    {
      if (!std::holds_alternative<DescriptorImageInfo>(write.resource))
        throw std::invalid_argument(
            "Descriptor resource type does not match image binding");
      const auto info = std::get<DescriptorImageInfo>(write.resource);
      const auto view = m_impl->views.find(info.image);
      if (view == m_impl->views.end() || info.image.owner != DeviceId() ||
          !m_impl->samplers.contains(info.sampler) ||
          info.sampler.owner != DeviceId())
        throw std::invalid_argument(
            "Invalid or cross-device descriptor image or sampler");
      const auto image = m_impl->images.find(view->second.image);
      if (image == m_impl->images.end() ||
          !image->second.usage.Contains(ImageUsage::Sampled))
        throw std::invalid_argument(
            "Descriptor image was not created for sampling");
      target = info;
    }
  }
  found->second.resources = std::move(updated);
}

ShaderModuleHandle
OpenGLRenderDevice::CreateShaderModule(const ShaderModuleDesc& desc)
{
  m_impl->ActivateContext();
  (void)ToGLShaderStage(desc.stage);
  if (desc.bytecode.empty() ||
      desc.bytecode.size_bytes() % sizeof(std::uint32_t) ||
      desc.entryPoint.empty())
    throw std::invalid_argument(
        "Shader module input must contain SPIR-V words and an entry point");
  const auto words = std::span<const std::uint32_t>(
      reinterpret_cast<const std::uint32_t*>(desc.bytecode.data()),
      desc.bytecode.size_bytes() / sizeof(std::uint32_t));
  if (words.front() != 0x07230203u)
    throw std::invalid_argument(
        "Shader module input must contain SPIR-V words");
  const auto handle =
      MakeHandle<ShaderModuleHandle>(m_impl->nextShader++, DeviceId());
  m_impl->shaders.emplace(handle, Impl::Shader{desc.stage,
                                               std::string(desc.entryPoint),
                                               {words.begin(), words.end()}});
  return handle;
}

GraphicsPipelineHandle
OpenGLRenderDevice::CreatePipeline(const GraphicsPipelineDesc& desc)
{
  m_impl->ActivateContext();
  std::vector<const Impl::Shader*> shaderModules;
  if (desc.computeShader)
  {
    const auto it = m_impl->shaders.find(desc.computeShader);
    if (it == m_impl->shaders.end() ||
        it->second.stage != ShaderStageFlags::Compute)
      throw std::invalid_argument("Pipeline requires a compute shader");
    shaderModules.push_back(&it->second);
  }
  else
  {
    const auto vert = m_impl->shaders.find(desc.vertexShader),
               frag = m_impl->shaders.find(desc.fragmentShader);
    if (vert == m_impl->shaders.end() || frag == m_impl->shaders.end() ||
        vert->second.stage != ShaderStageFlags::Vertex ||
        frag->second.stage != ShaderStageFlags::Fragment)
      throw std::invalid_argument(
          "Pipeline requires vertex and fragment shaders");
    shaderModules = {&vert->second, &frag->second};
  }
  const auto layoutIt = m_impl->pipelineLayouts.find(desc.layout);
  if (!desc.layout || layoutIt == m_impl->pipelineLayouts.end())
    throw std::invalid_argument("Pipeline requires a valid pipeline layout");
  GLint ubos{}, ssbos{}, textures{}, images{}, atomics{};
  glGetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &ubos);
  glGetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &ssbos);
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &textures);
  glGetIntegerv(GL_MAX_IMAGE_UNITS, &images);
  glGetIntegerv(GL_MAX_ATOMIC_COUNTER_BUFFER_BINDINGS, &atomics);
  const Detail::OpenGLBindingLimits limits{
      static_cast<std::uint32_t>(std::max(ubos, 0)),
      static_cast<std::uint32_t>(std::max(ssbos, 0)),
      static_cast<std::uint32_t>(std::max(textures, 0)),
      static_cast<std::uint32_t>(std::max(images, 0)),
      static_cast<std::uint32_t>(std::max(atomics, 0))};
  std::uint32_t neededUbo{}, neededSsbo{}, neededTexture{};
  for (const auto& [_, location] : layoutIt->second.locations)
  {
    auto& total = location.type == DescriptorType::UniformBuffer ? neededUbo
                  : location.type == DescriptorType::StorageBuffer
                      ? neededSsbo
                      : neededTexture;
    total = std::max(total, location.bindingPoint + location.count);
  }
  if (neededUbo > limits.uniformBuffers || neededSsbo > limits.storageBuffers ||
      neededTexture > limits.textureUnits)
    throw std::runtime_error(
        "Pipeline layout exceeds an OpenGL binding-point limit");
  auto compile = [&](const Impl::Shader& source)
  {
    const auto glsl = Detail::TranslateShaderToOpenGL(
        source.words, source.stage, source.entryPoint, limits,
        &layoutIt->second.locations);
    const auto shader = glCreateShader(ToGLShaderStage(source.stage));
    try
    {
      CompileGLSL(shader, glsl);
    }
    catch (...)
    {
      glDeleteShader(shader);
      throw;
    }
    return shader;
  };
  const auto program = glCreateProgram();
  std::vector<GLuint> compiled;
  try
  {
    for (auto* module : shaderModules)
    {
      auto shader = compile(*module);
      compiled.push_back(shader);
      glAttachShader(program, shader);
    }
    glLinkProgram(program);
  }
  catch (...)
  {
    for (auto shader : compiled)
      glDeleteShader(shader);
    glDeleteProgram(program);
    throw;
  }
  for (auto shader : compiled)
  {
    glDetachShader(program, shader);
    glDeleteShader(shader);
  }
  GLint ok{};
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    GLint size{};
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
    std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0');
    glGetProgramInfoLog(program, size, nullptr, log.data());
    glDeleteProgram(program);
    throw std::runtime_error("OpenGL pipeline link failed: " + log);
  }
  for (const auto& [_, location] : layoutIt->second.locations)
  {
    if (location.type != DescriptorType::CombinedImageSampler)
      continue;
    for (const auto* shaderModule : shaderModules)
    {
      spirv_cross::CompilerGLSL reflection(shaderModule->words.data(),
                                           shaderModule->words.size());
      const auto model = shaderModule->stage == ShaderStageFlags::Vertex
                             ? spv::ExecutionModelVertex
                         : shaderModule->stage == ShaderStageFlags::Compute
                             ? spv::ExecutionModelGLCompute
                             : spv::ExecutionModelFragment;
      reflection.set_entry_point(shaderModule->entryPoint, model);
      const auto resources = reflection.get_shader_resources();
      auto assign = [&](const auto& list)
      {
        for (const auto& resource : list)
        {
          const auto set = reflection.has_decoration(
                               resource.id, spv::DecorationDescriptorSet)
                               ? reflection.get_decoration(
                                     resource.id, spv::DecorationDescriptorSet)
                               : 0u;
          const auto binding =
              reflection.has_decoration(resource.id, spv::DecorationBinding)
                  ? reflection.get_decoration(resource.id,
                                              spv::DecorationBinding)
                  : 0u;
          const auto mapped = layoutIt->second.locations.find({set, binding});
          if (mapped == layoutIt->second.locations.end())
            continue;
          const auto type = reflection.get_type(resource.type_id);
          const auto count = type.array.empty() ? 1u : type.array.front();
          if (count > mapped->second.count)
            throw std::invalid_argument(
                "Shader descriptor array exceeds pipeline layout count");
          std::vector<GLint> units(count);
          for (std::uint32_t i = 0; i < count; ++i)
            units[i] = static_cast<GLint>(mapped->second.bindingPoint + i);
          const auto uniform =
              glGetUniformLocation(program, resource.name.c_str());
          if (uniform >= 0)
            glProgramUniform1iv(program, uniform, static_cast<GLsizei>(count),
                                units.data());
        }
      };
      assign(resources.sampled_images);
      assign(resources.separate_images);
    }
  }
  const auto handle =
      MakeHandle<GraphicsPipelineHandle>(m_impl->nextPipeline++, DeviceId());
  Impl::Pipeline pipeline{program, desc.vertexLayout, desc.layout, {}};
  pipeline.topology = desc.topology;
  m_impl->pipelines.emplace(handle, std::move(pipeline));
  return handle;
}

std::unique_ptr<IRenderCommandList>
OpenGLRenderDevice::CreateCommandList(QueueType)
{
  return std::make_unique<OpenGLCommandList>();
}
void OpenGLRenderDevice::DestroyImage(ImageHandle h)
{
  m_impl->ActivateContext();
  auto i = m_impl->images.find(h);
  if (i != m_impl->images.end())
  {
    glDeleteTextures(1, &i->second.id);
    m_impl->images.erase(i);
    for (auto view = m_impl->views.begin(); view != m_impl->views.end();)
      if (view->second.image == h)
      {
        const auto viewHandle = view->first;
        glDeleteTextures(1, &view->second.id);
        view = m_impl->views.erase(view);
        for (auto& [_, set] : m_impl->sets)
          for (auto& [__, values] : set.resources)
            for (auto& value : values)
              if (auto info = std::get_if<DescriptorImageInfo>(&value);
                  info && info->image == viewHandle)
                value = std::monostate{};
      }
      else
        ++view;
  }
}
void OpenGLRenderDevice::DestroyImageView(ImageViewHandle h)
{
  m_impl->ActivateContext();
  auto i = m_impl->views.find(h);
  if (i != m_impl->views.end())
  {
    glDeleteTextures(1, &i->second.id);
    m_impl->views.erase(i);
    for (auto& [_, set] : m_impl->sets)
      for (auto& [__, values] : set.resources)
        for (auto& value : values)
          if (auto info = std::get_if<DescriptorImageInfo>(&value);
              info && info->image == h)
            value = std::monostate{};
  }
}
void OpenGLRenderDevice::DestroyBuffer(BufferHandle h)
{
  m_impl->ActivateContext();
  auto i = m_impl->buffers.find(h);
  if (i != m_impl->buffers.end())
  {
    glDeleteBuffers(1, &i->second.id);
    m_impl->buffers.erase(i);
    for (auto& [_, set] : m_impl->sets)
      for (auto& [__, values] : set.resources)
        for (auto& value : values)
          if (auto info = std::get_if<DescriptorBufferInfo>(&value);
              info && info->buffer == h)
            value = std::monostate{};
  }
}
void OpenGLRenderDevice::DestroyPipeline(GraphicsPipelineHandle h)
{
  auto i = m_impl->pipelines.find(h);
  if (i == m_impl->pipelines.end())
    return;
  m_impl->ActivateContext();
  glDeleteProgram(i->second.program);
  for (std::uint32_t index = 0; index < m_impl->swapchains.size(); ++index)
  {
    auto& slot = m_impl->swapchains[index];
    if (!slot.value)
      continue;
    const SwapchainHandle swapchain{index, slot.generation, DeviceId()};
    if (const auto vao = i->second.vaos.find(swapchain);
        vao != i->second.vaos.end())
    {
      m_impl->ActivateContext(slot.value.get());
      glDeleteVertexArrays(1, &vao->second);
      i->second.vaos.erase(vao);
    }
  }
  m_impl->ActivateContext();
  m_impl->pipelines.erase(i);
}
void OpenGLRenderDevice::DestroyModule(ShaderModuleHandle h)
{
  m_impl->shaders.erase(h);
}
void OpenGLRenderDevice::DestroyDescriptorSet(DescriptorSetHandle h)
{
  m_impl->sets.erase(h);
  for (auto it = m_impl->boundSets.begin(); it != m_impl->boundSets.end();)
    if (it->second == h)
      it = m_impl->boundSets.erase(it);
    else
      ++it;
}
void OpenGLRenderDevice::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle h)
{
  if (!m_impl->setLayouts.contains(h))
    return;
  for (auto it = m_impl->sets.begin(); it != m_impl->sets.end();)
    if (it->second.layout == h)
      it = m_impl->sets.erase(it);
    else
      ++it;
  for (auto it = m_impl->pipelineLayouts.begin();
       it != m_impl->pipelineLayouts.end();)
  {
    if (std::find(it->second.sets.begin(), it->second.sets.end(), h) !=
        it->second.sets.end())
    {
      const auto dead = it->first;
      for (auto p = m_impl->pipelines.begin(); p != m_impl->pipelines.end();)
        if (p->second.pipelineLayout == dead)
        {
          DestroyPipeline(p->first);
          p = m_impl->pipelines.begin();
        }
        else
          ++p;
      it = m_impl->pipelineLayouts.erase(it);
    }
    else
      ++it;
  }
  m_impl->setLayouts.erase(h);
}
void OpenGLRenderDevice::DestroyPipelineLayout(PipelineLayoutHandle h)
{
  if (!m_impl->pipelineLayouts.contains(h))
    return;
  for (auto p = m_impl->pipelines.begin(); p != m_impl->pipelines.end();)
    if (p->second.pipelineLayout == h)
    {
      const auto pipeline = p->first;
      ++p;
      DestroyPipeline(pipeline);
    }
    else
      ++p;
  m_impl->pipelineLayouts.erase(h);
}
void OpenGLRenderDevice::DestroySampler(SamplerHandle h)
{
  const auto i = m_impl->samplers.find(h);
  if (i == m_impl->samplers.end())
    return;
  m_impl->ActivateContext();
  glDeleteSamplers(1, &i->second);
  m_impl->samplers.erase(i);
  for (auto& [_, set] : m_impl->sets)
    for (auto& [__, values] : set.resources)
      for (auto& value : values)
        if (auto info = std::get_if<DescriptorImageInfo>(&value);
            info && info->sampler == h)
          value = std::monostate{};
}
RenderCapabilities OpenGLRenderDevice::GetCapabilities() const
{
  m_impl->ActivateContext();
  const auto* renderer = glGetString(GL_RENDERER);
  RenderCapabilities caps;
  caps.deviceName =
      renderer ? reinterpret_cast<const char*>(renderer) : "OpenGL";
  GLint major{}, minor{};
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);
  caps.apiVersion = (static_cast<unsigned>(major) << 22) |
                    (static_cast<unsigned>(minor) << 12);
  caps.headless = true;
  caps.computeShaders = true;
#ifdef LIGHTHOUSE_IMGUI
  caps.imgui = true;
#endif
  GLint extensionCount{};
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
  for (GLint i = 0; i < extensionCount; ++i)
  {
    const auto* extension =
        reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
    if (!extension)
      continue;
    if (std::strcmp(extension, "GL_ARB_bindless_texture") == 0)
      caps.supportedBindlessResources = true;
    if (std::strcmp(extension, "GL_NV_mesh_shader") == 0 ||
        std::strcmp(extension, "GL_EXT_mesh_shader") == 0)
      caps.supportedMeshShaders = true;
  }
  // Bindless/multiview are not enabled by the current OpenGL binding API.
  return caps;
}

ImageViewHandle
OpenGLRenderDevice::AcquiredImageView(SwapchainHandle swapchain) const noexcept
{
  const auto* target = m_impl->FindSwapchain(swapchain);
  return target && target->acquired ? target->images[target->imageIndex].view
                                    : ImageViewHandle{};
}

void OpenGLRenderDevice::Submit(const IRenderCommandList& list)
{
  const auto* commands = dynamic_cast<const OpenGLCommandList*>(&list);
  if (!commands)
    throw std::invalid_argument(
        "OpenGL submit received a command list from another backend");
  if (m_impl->activeSwapchain)
    m_impl->ActivateContext(m_impl->FindSwapchain(*m_impl->activeSwapchain));
  else
    m_impl->ActivateContext();
  m_impl->submissionWrites.clear();
  try
  {
    for (const auto& command : commands->Commands())
      std::visit([this](const auto& value) { value.Execute(this); }, command);
    for (auto handle : m_impl->submissionWrites)
      m_impl->FindSwapchain(handle)->frameReady = true;
    m_impl->submissionWrites.clear();
  }
  catch (...)
  {
    for (auto handle : m_impl->submissionWrites)
      m_impl->FindSwapchain(handle)->frameReady = false;
    m_impl->submissionWrites.clear();
    throw;
  }
}

void OpenGLRenderDevice::Present(SwapchainHandle swapchain)
{
  auto* target = m_impl->FindSwapchain(swapchain);
  if (!target)
    throw std::invalid_argument(
        "OpenGL present received an invalid swapchain handle");
  if (!target->acquired)
    throw std::logic_error(
        "OpenGL present requires an acquired image for this swapchain");
  if (!target->frameReady)
    throw std::logic_error("OpenGL present requires successfully submitted "
                           "writes to this swapchain");
  m_impl->ActivateContext(target);
  const auto& image = target->images.at(target->imageIndex);
  GLint oldRead{}, oldDraw{};
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
  const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, image.framebuffer);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
  glDrawBuffer(GL_BACK);
  EGLint width{}, height{};
  if (!eglQuerySurface(m_impl->group->display, target->surface, EGL_WIDTH,
                       &width) ||
      !eglQuerySurface(m_impl->group->display, target->surface, EGL_HEIGHT,
                       &height))
  {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, oldRead);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDraw);
    if (scissor)
      glEnable(GL_SCISSOR_TEST);
    throw std::runtime_error("Failed to query the OpenGL presentation extent");
  }
  glBlitFramebuffer(0, 0, target->width, target->height, 0, 0, width, height,
                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, oldRead);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDraw);
  if (scissor)
    glEnable(GL_SCISSOR_TEST);
  if (eglSwapBuffers(m_impl->group->display, target->surface) != EGL_TRUE)
    throw std::runtime_error("EGL failed to present the rendered frame");
  target->imageIndex = (target->imageIndex + 1) % target->images.size();
  target->acquired = false;
  target->frameReady = false;
  if (m_impl->activeSwapchain == swapchain)
    m_impl->activeSwapchain.reset();
}
void OpenGLRenderDevice::WaitIdle()
{
  // Work can be submitted to any window context as well as the shared context.
  m_impl->ActivateContext();
  glFinish();
  for (const auto& slot : m_impl->swapchains)
  {
    if (!slot.value)
      continue;
    m_impl->ActivateContext(slot.value.get());
    glFinish();
  }
  if (m_impl->activeSwapchain)
    m_impl->ActivateContext(m_impl->FindSwapchain(*m_impl->activeSwapchain));
  else
    m_impl->ActivateContext();
}

#ifdef LIGHTHOUSE_IMGUI
OpenGLRenderDevice::Impl::ContextSnapshot
OpenGLRenderDevice::Impl::ContextSnapshot::Capture()
{
  return {glfwGetCurrentContext(), eglGetCurrentDisplay(),
          eglGetCurrentContext(), eglGetCurrentSurface(EGL_READ),
          eglGetCurrentSurface(EGL_DRAW)};
}
void OpenGLRenderDevice::Impl::ContextSnapshot::Restore() const noexcept
{
  if (glfwGetCurrentContext())
    glfwMakeContextCurrent(nullptr);
  if (glfw)
    glfwMakeContextCurrent(glfw);
  else if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT)
    eglMakeCurrent(display, draw, read, context);
  else if (const auto current = eglGetCurrentDisplay();
           current != EGL_NO_DISPLAY)
    eglMakeCurrent(current, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}
namespace
{
void ImGuiWindowHints()
{
  // Engine creation resets global GLFW hints to GLFW_NO_API. Reset these
  // before every viewport update, which may create a new ImGui window.
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
  glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
}
} // namespace
void OpenGLRenderDevice::InitializeImGui(WindowManager& windows)
{
  auto* context = ImGui::GetCurrentContext();
  if (m_impl->imgui.contains(context))
    return;
  windows.Initialize();
  const auto saved = Impl::ContextSnapshot::Capture();
  GLFWwindow* host{};
  bool platformInitialized = false, rendererInitialized = false;
  try
  {
    // Initialize the engine's shared EGL context even for an ImGui-only
    // renderer. The ImGui GLFW contexts have their own shared resources.
    m_impl->group->Initialize(glfwGetX11Display());
    ImGuiWindowHints();
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    host =
        glfwCreateWindow(1280, 720, "Lighthouse ImGui host", nullptr, nullptr);
    if (!host)
      throw std::runtime_error("Failed to create the hidden ImGui OpenGL host");
    glfwMakeContextCurrent(host);
    glfwSwapInterval(0);
    if (!ImGui_ImplGlfw_InitForOpenGL(host, true))
      throw std::runtime_error("Failed to initialize ImGui GLFW viewports");
    platformInitialized = true;
    if (!(ImGui::GetIO().BackendFlags & ImGuiBackendFlags_PlatformHasViewports))
      throw std::runtime_error(
          "The selected GLFW platform does not support ImGui multi-viewports");
    if (!ImGui_ImplOpenGL3_Init("#version 450 core"))
      throw std::runtime_error("Failed to initialize ImGui OpenGL viewports");
    rendererInitialized = true;
    m_impl->imgui.emplace(context, Impl::ImGuiState{.host = host});
  }
  catch (...)
  {
    if (rendererInitialized)
      ImGui_ImplOpenGL3_Shutdown();
    if (platformInitialized)
      ImGui_ImplGlfw_Shutdown();
    if (host)
    {
      glfwMakeContextCurrent(nullptr);
      glfwDestroyWindow(host);
    }
    glfwDefaultWindowHints();
    saved.Restore();
    throw;
  }
  glfwDefaultWindowHints();
  saved.Restore();
}
void OpenGLRenderDevice::ShutdownImGui() noexcept
{
  const auto found = m_impl->imgui.find(ImGui::GetCurrentContext());
  if (found == m_impl->imgui.end())
    return;
  EndImGuiFrame();
  auto saved = Impl::ContextSnapshot::Capture();
  const auto host = found->second.host;
  if (saved.glfw == host)
    saved = {};
  glfwMakeContextCurrent(host);
  ImGui::DestroyPlatformWindows();
  Detail::RemoveImGuiGlfwCallbacks(ImGui::GetCurrentContext());
  glfwMakeContextCurrent(host);
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  glfwMakeContextCurrent(nullptr);
  glfwDestroyWindow(host);
  m_impl->imgui.erase(found);
  saved.Restore();
}
void OpenGLRenderDevice::BeginImGuiFrame()
{
  auto& state = m_impl->imgui.at(ImGui::GetCurrentContext());
  if (state.frameActive)
    throw std::logic_error("ImGui frame already active");
  state.saved = Impl::ContextSnapshot::Capture();
  state.frameActive = true;
  try
  {
    glfwMakeContextCurrent(state.host);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
  }
  catch (...)
  {
    EndImGuiFrame();
    throw;
  }
}
void OpenGLRenderDevice::RenderImGuiViewports()
{
  ImGuiWindowHints();
  // The primary host stays hidden. No engine window is registered with ImGui.
  ImGui::UpdatePlatformWindows();
  Detail::RouteImGuiGlfwCallbacks();
  ImGui::RenderPlatformWindowsDefault();
  glfwDefaultWindowHints();
}
void OpenGLRenderDevice::EndImGuiFrame() noexcept
{
  const auto found = m_impl->imgui.find(ImGui::GetCurrentContext());
  if (found == m_impl->imgui.end() || !found->second.frameActive)
    return;
  found->second.saved.Restore();
  found->second.frameActive = false;
}
#endif

} // namespace Lighthouse
