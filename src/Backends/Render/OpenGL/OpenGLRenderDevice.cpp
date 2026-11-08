#include "OpenGLCommandList.hpp"
#include "OpenGLContextExecutor.hpp"
#include "OpenGLDeviceGroup.hpp"
#include "OpenGLRenderDeviceImpl.hpp"
#include "OpenGLShaderTranslator.hpp"
#include <Lighthouse/Backends/Render/OpenGL/OpenGLRenderDevice.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Engine/Rendering/Detail/ImGuiGlfwRouting.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <chrono>
#include <spirv_cross/spirv_glsl.hpp>
#include <thread>

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
class OpenGLRenderDevice::Queue final : public IRenderQueue
{
  OpenGLRenderDevice& device;
  Impl::ContextState state;
  Impl::Execution execution;
  SurfaceHandle presentSurface;
  struct Flight
  {
    Impl::SyncPtr sync;
    std::shared_ptr<Impl::Snapshot> snapshot;
  };
  std::deque<Flight> flights;
  std::exception_ptr pendingFailure;
  Detail::OpenGLContextExecutor worker;

  void Collect()
  {
    while (!flights.empty())
    {
      const auto status = glClientWaitSync(flights.front().sync->native, 0, 0);
      if (status == GL_WAIT_FAILED)
        throw std::runtime_error("OpenGL completion query failed");
      if (status == GL_TIMEOUT_EXPIRED)
        break;
      flights.pop_front();
    }
    device.m_impl->deletions->Drain();
  }
  void DrainGPU()
  {
    auto sync = device.m_impl->InsertSync();
    if (glClientWaitSync(sync->native, 0, GL_TIMEOUT_IGNORED) == GL_WAIT_FAILED)
      throw std::runtime_error("OpenGL queue completion wait failed");
    flights.clear();
    device.m_impl->deletions->Drain();
  }
  static void WaitPoint(const std::shared_ptr<Impl::Point>& point)
  {
    // Only CPU publication is coordinated here, never GPU completion. The
    // producing submission was already reserved before this wait was accepted.
    const auto sync = point->AwaitIssue();
    if (sync)
      glWaitSync(sync->native, 0, GL_TIMEOUT_IGNORED);
  }

public:
  explicit Queue(OpenGLRenderDevice& d, SurfaceHandle surface)
      : device(d), execution{d.m_impl.get(), &state}, presentSurface(surface),
        worker(
            d.m_impl->group, [this] { Impl::executing = &execution; },
            [this]
            {
              try
              {
                DrainGPU();
              }
              catch (...)
              {
                flights.clear();
              }
              state.Clear();
              device.m_impl->deletions->Drain();
              Impl::executing = nullptr;
            })
  {
  }
  ~Queue() override
  {
    worker.Stop();
  }
  Flags<RenderQueueCapability> GetCapabilities() const noexcept override
  {
    Flags<RenderQueueCapability> caps{RenderQueueCapability::Graphics,
                                      RenderQueueCapability::Compute,
                                      RenderQueueCapability::Transfer};
    if (device.m_impl->group->nativeDisplay)
      caps |= RenderQueueCapability::Present;
    return caps;
  }
  void CollectLater()
  {
    try
    {
      worker.Post([this] { Collect(); });
    }
    catch (const std::logic_error&)
    {
    } // Shutdown drains all retained work.
    catch (const std::bad_alloc&)
    {
    } // Best-effort retirement must not change fence status.
  }
  void StopAccepting()
  {
    worker.StopAccepting();
  }
  void Stop()
  {
    worker.Stop();
  }
  void RetirePipeline(GraphicsPipelineHandle h)
  {
    worker.Post(
        [this, h]
        {
          const auto found = state.vaos.find(h);
          if (found != state.vaos.end())
          {
            glDeleteVertexArrays(1, &found->second);
            state.vaos.erase(found);
          }
        });
  }
  Impl::SyncPtr Handoff()
  {
    return worker.Invoke(
        [this]
        {
          worker.RethrowFailure();
          Collect();
          return device.m_impl->InsertSync();
        });
  }
  void Submit(const QueueSubmitInfo&, FenceHandle) override;
  void Present(SwapchainHandle, std::span<const SemaphoreHandle>) override;
  void WaitIdle() override
  {
    worker.Invoke(
        [this]
        {
          worker.RethrowFailure();
          DrainGPU();
          auto error = std::exchange(pendingFailure, {});
          if (error)
            std::rethrow_exception(error);
        });
  }
};

IRenderQueue* OpenGLRenderDevice::GetQueue(std::uint32_t index)
{
  return index < m_queues.size() ? m_queues[index].get() : nullptr;
}
SurfaceHandle OpenGLRenderDevice::CreateSurface(Window& window)
{
  Impl::Scope resourceScope(*m_impl);
  if (!window.NativeDisplayHandle() || !window.NativeEGLWindowHandle())
    throw std::invalid_argument(
        "OpenGL surfaces require an EGL-enabled window");
  m_impl->group->Initialize(window.NativeDisplayHandle());
  if (m_impl->group->nativeDisplay != window.NativeDisplayHandle())
    throw std::invalid_argument("Surface belongs to another EGL display");
  const SurfaceHandle h{m_impl->nextSurface++, 1, DeviceId()};
  m_impl->surfaces.emplace(h, &window);
  return h;
}
void OpenGLRenderDevice::DestroySurface(SurfaceHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  for (const auto& slot : m_impl->swapchains)
    if (slot.value && slot.value->surfaceHandle == h)
      throw std::logic_error("Destroy swapchains before their surface");
  m_impl->surfaces.erase(h);
}

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
  case ImageFormat::RGB10A2:
  case ImageFormat::BGR10A2:
    return GL_RGB10_A2;
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
    if (const auto view = resources.views.find(image.view);
        view != resources.views.end())
    {
      resources.views.erase(view);
    }
    if (const auto texture = resources.images.find(image.image);
        texture != resources.images.end())
    {
      resources.images.erase(texture);
    }
    for (auto& [_, set] : resources.sets)
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
      resources.images.emplace(
          image.image,
          ImageResource{
              texture,
              ImageUsage::ColorAttachment | ImageUsage::TransferSource |
                  ImageUsage::TransferDestination | ImageUsage::Sampled,
              ImageType::Image2D, swapchain.info.imageFormat});
      glTextureStorage2D(texture, 1,
                         ToGLImageFormat(swapchain.info.imageFormat),
                         swapchain.width, swapchain.height);
      glGenTextures(1, &view);
      image.view = MakeHandle<ImageViewHandle>(nextView++, owner);
      resources.views.emplace(image.view, ImageViewResource{view, image.image});
      resources.images.at(image.image).lifetime =
          Keep([texture] { glDeleteTextures(1, &texture); });
      resources.views.at(image.view).lifetime =
          Keep([view] { glDeleteTextures(1, &view); });
      glTextureView(view, GL_TEXTURE_2D, texture,
                    ToGLImageFormat(swapchain.info.imageFormat), 0, 1, 0, 1);
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
  Impl::Scope resourceScope(*m_impl);
  auto* target = m_impl->FindSwapchain(swapchain);
  if (!target)
    throw std::invalid_argument(
        "OpenGL acquire received an invalid swapchain handle");
  std::unique_lock targetLock(target->mutex);
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
    replacement.info = target->info;
    m_impl->CreateSwapchainImages(replacement, count);
    m_impl->DestroySwapchainImages(*target);
    target->images = std::move(replacement.images);
    target->width = replacement.width;
    target->height = replacement.height;
    target->imageIndex = 0;
  }
  m_impl->State().activeSwapchain = swapchain;
  target->acquired = true;
  target->frameReady = false;
  const auto& image = target->images.at(target->imageIndex);
  return {.image = image.image, .view = image.view};
}

OpenGLRenderDevice::OpenGLRenderDevice(
    std::shared_ptr<Detail::OpenGLDeviceGroup> group,
    const RenderDeviceCreateInfo& config,
    std::span<const std::pair<SurfaceHandle, Window*>> surfaces)
    : m_impl(std::make_unique<Impl>(std::move(group), DeviceId()))
{
  Impl::Scope scope(*m_impl);
  for (auto [surface, window] : surfaces)
    m_impl->surfaces.emplace(surface, window);
  // EGL has no maximum-context-count query. Bound dedicated worker allocation
  // rather than attempting an unbounded preferred count.
  constexpr std::uint32_t WorkerBudget = 64;
  std::uint64_t minimum = 0;
  std::vector<bool> supported;
  for (const auto& request : config.queues)
  {
    if (request.minCount > request.preferredCount)
      throw std::invalid_argument("Queue minimum exceeds preferred count");
    minimum += request.minCount;
    const auto bits = static_cast<std::uint32_t>(request.capabilities);
    bool available = (bits & ~15u) == 0;
    if (request.preferredCount &&
        (bits & static_cast<std::uint32_t>(RenderQueueCapability::Present)))
    {
      const auto target = m_impl->surfaces.find(request.presentSurface);
      if (target == m_impl->surfaces.end())
        throw std::invalid_argument(
            "Presentation request needs a backend-created surface");
      available = available &&
                  !EnumerateAvailableSwapchainFormats(*target->second).empty();
    }
    if (request.minCount && !available)
      throw std::runtime_error(
          "Required OpenGL queue capabilities unavailable");
    supported.push_back(available);
  }
  if (minimum > WorkerBudget)
    throw std::runtime_error(
        "Required OpenGL queue count exceeds the 64-worker device budget");
  std::vector<std::vector<std::unique_ptr<IRenderQueue>>> queues(
      config.queues.size());
  std::uint32_t count = 0;
  const auto create = [&](size_t request)
  {
    const auto surface =
        (static_cast<std::uint32_t>(config.queues[request].capabilities) & 8u)
            ? config.queues[request].presentSurface
            : SurfaceHandle{};
    queues[request].push_back(std::make_unique<Queue>(*this, surface));
    ++count;
  };
  try
  {
    for (size_t request = 0; request < config.queues.size(); ++request)
      for (std::uint32_t i = 0; i < config.queues[request].minCount; ++i)
        create(request);
    for (size_t request = 0; request < config.queues.size(); ++request)
    {
      if (!supported[request])
        continue;
      while (queues[request].size() < config.queues[request].preferredCount &&
             count < WorkerBudget)
      {
        try
        {
          create(request);
        }
        catch (const std::runtime_error&)
        {
          break;
        }
        catch (const std::bad_alloc&)
        {
          break;
        }
      }
    }
    m_queues.reserve(count);
    for (auto& requested : queues)
      for (auto& queue : requested)
        m_queues.push_back(std::move(queue));
  }
  catch (...)
  {
    // These workers have no submitted work and need no device metadata to stop.
    for (auto& queue : m_queues)
      static_cast<Queue&>(*queue).StopAccepting();
    for (auto& requested : queues)
      for (auto& queue : requested)
        if (queue)
          static_cast<Queue&>(*queue).StopAccepting();
    m_queues.clear();
    for (auto& requested : queues)
      requested.clear();
    throw;
  }
}
OpenGLRenderDevice::~OpenGLRenderDevice()
{
  for (auto& queue : m_queues)
    static_cast<Queue&>(*queue).StopAccepting();
  for (auto& queue : m_queues)
    static_cast<Queue&>(*queue).Stop();
  m_queues.clear();
  try
  {
    Impl::Scope scope(*m_impl, false);
    auto resourceCompletion = m_impl->InsertSync();
    if (glClientWaitSync(resourceCompletion->native, 0, GL_TIMEOUT_IGNORED) ==
        GL_WAIT_FAILED)
      throw std::runtime_error("Resource-context shutdown wait failed");
    for (auto& slot : m_impl->swapchains)
    {
      if (!slot.value)
        continue;
      m_impl->ActivateContext();
      eglDestroySurface(m_impl->group->display, slot.value->surface);
      slot.value.reset();
    }
    m_impl->hostState.Clear();
    m_impl->resources = {};
    m_impl->fences.clear();
    m_impl->semaphores.clear();
    m_impl->deletions->Drain();
  }
  catch (...)
  {
    // Context loss must not leave threads or native window surfaces alive.
    for (auto& slot : m_impl->swapchains)
      if (slot.value)
      {
        eglDestroySurface(m_impl->group->display, slot.value->surface);
        slot.value.reset();
      }
  }
}

std::vector<SwapchainFormat>
OpenGLRenderDevice::EnumerateAvailableSwapchainFormats(Window& window)
{
  Impl::Scope resourceScope(*m_impl);
  if (!window.NativeDisplayHandle() || !window.NativeEGLWindowHandle())
    throw std::invalid_argument(
        "OpenGL format query requires an EGL-enabled window");
  // EGL forbids a second surface for a native window already in use. Return
  // the capability snapshot taken immediately before its surface was created.
  for (const auto& slot : m_impl->swapchains)
    if (slot.value &&
        slot.value->nativeWindow == window.NativeEGLWindowHandle())
      return slot.value->availableFormats;
  m_impl->group->Initialize(window.NativeDisplayHandle());
  std::vector<SwapchainFormat> result;
  for (auto format :
       {SwapchainFormat::SRGB, SwapchainFormat::HDR10, SwapchainFormat::ScRGB})
    for (auto config : m_impl->group->WindowConfigs(window, format))
    {
      EGLSurface surface = EGL_NO_SURFACE;
      try
      {
        surface = m_impl->group->CreateWindowSurface(
            window.NativeEGLWindowHandle(), config, format);
        const auto context = m_impl->group->CreateSharedContext(config);
        eglDestroyContext(m_impl->group->display, context);
        eglDestroySurface(m_impl->group->display, surface);
        result.push_back(format);
        break;
      }
      catch (const std::runtime_error&)
      {
        if (surface != EGL_NO_SURFACE)
          eglDestroySurface(m_impl->group->display, surface);
      }
    }
  return result;
}

std::optional<SwapchainInfo>
OpenGLRenderDevice::GetSwapchainInfo(SwapchainHandle handle) const
{
  Impl::ReadScope metadataRead(*m_impl);
  const auto* swap = m_impl->FindSwapchain(handle);
  return swap ? std::optional(swap->info) : std::nullopt;
}

SwapchainHandle
OpenGLRenderDevice::CreateSwapchain(SurfaceHandle surface,
                                    const SwapchainConfig& config)
{
  Impl::Scope resourceScope(*m_impl);
  auto& window = *m_impl->surfaces.at(surface);
  if (config.minImageCount == 0)
    throw std::invalid_argument("Swapchain minImageCount must be at least one");
  if (!window.NativeDisplayHandle() || !window.NativeEGLWindowHandle())
    throw std::invalid_argument(
        "OpenGL swapchain requires an EGL-enabled window from WindowManager");
  m_impl->group->Initialize(window.NativeDisplayHandle());
  if (m_impl->group->nativeDisplay != window.NativeDisplayHandle())
    throw std::invalid_argument(
        "All swapchains on an OpenGL device must use the same EGL display");
  auto state = std::make_shared<Impl::Swapchain>();
  state->surfaceHandle = surface;
  state->vsync = config.vsync;
  state->nativeWindow = window.NativeEGLWindowHandle();
  state->availableFormats = EnumerateAvailableSwapchainFormats(window);
  state->info = {config.format, config.format == SwapchainFormat::ScRGB
                                    ? ImageFormat::RGBA16f
                                : config.format == SwapchainFormat::HDR10
                                    ? ImageFormat::RGB10A2
                                    : ImageFormat::RGBA8};
  for (auto nativeConfig : m_impl->group->WindowConfigs(window, config.format))
  {
    try
    {
      state->surface = m_impl->group->CreateWindowSurface(
          window.NativeEGLWindowHandle(), nativeConfig, config.format);
      break;
    }
    catch (const std::runtime_error&)
    {
      if (state->surface != EGL_NO_SURFACE)
        eglDestroySurface(m_impl->group->display, state->surface);
      state->surface = EGL_NO_SURFACE;
    }
  }
  if (state->surface == EGL_NO_SURFACE)
    throw std::runtime_error(
        "Requested OpenGL swapchain format is unavailable for this window");
  try
  {
    m_impl->CreateSwapchainImages(*state, config.minImageCount);
  }
  catch (...)
  {
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
  Impl::Scope resourceScope(*m_impl);
  if (!handle || handle.owner != DeviceId() ||
      handle.index >= m_impl->swapchains.size())
    return false;
  auto& slot = m_impl->swapchains[handle.index];
  auto retained = slot.value;
  if (!slot.value || slot.generation != handle.generation)
    return false;
  std::unique_lock targetLock(slot.value->mutex);
  slot.value->dead = true;
  if (m_impl->State().activeSwapchain == handle)
    m_impl->State().activeSwapchain.reset();
  m_impl->ActivateContext(slot.value.get());
  m_impl->DestroySwapchainImages(*slot.value);

  eglDestroySurface(m_impl->group->display, slot.value->surface);
  slot.value.reset();
  if (++slot.generation == 0)
    ++slot.generation;
  return true;
}
bool OpenGLRenderDevice::IsValid(SwapchainHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  return m_impl->FindSwapchain(handle) != nullptr;
}

ImageHandle OpenGLRenderDevice::CreateImage(const ImageDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
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
  m_impl->resources.images.emplace(
      handle,
      Impl::ImageResource{
          texture, desc.usage, desc.type, desc.format,
          m_impl->Keep([texture] { glDeleteTextures(1, &texture); })});
  return handle;
}
std::vector<std::byte> OpenGLRenderDevice::ReadImage(ImageHandle handle)
{
  std::vector<Impl::SyncPtr> producers;
  for (auto& queue : m_queues)
    producers.push_back(static_cast<Queue&>(*queue).Handoff());
  Impl::Scope resourceScope(*m_impl);
  for (const auto& producer : producers)
    glWaitSync(producer->native, 0, GL_TIMEOUT_IGNORED);
  glMemoryBarrier(GL_ALL_BARRIER_BITS);
  // glGetTextureImage provides the blocking readback; context handoffs order
  // producers.
  m_impl->ActivateContext();
  const auto& image = m_impl->resources.images.at(handle);
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
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  const auto source = m_impl->resources.images.at(desc.image).id;
  GLuint view{};
  glGenTextures(1, &view);
  glTextureView(view, ToGLImageType(desc.type), source,
                ToGLImageFormat(desc.format), desc.baseMipLevel,
                desc.mipLevelCount, desc.baseArrayLayer, desc.arrayLayerCount);
  const auto handle =
      MakeHandle<ImageViewHandle>(m_impl->nextView++, DeviceId());
  m_impl->resources.views.emplace(
      handle,
      Impl::ImageViewResource{
          view, desc.image,
          m_impl->Keep([view] { glDeleteTextures(1, &view); })});
  return handle;
}

BufferHandle OpenGLRenderDevice::CreateBuffer(const BufferDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  if (desc.initialData.size_bytes() > desc.size)
    throw std::invalid_argument("Buffer initial data exceeds buffer size");
  GLuint id{};
  glCreateBuffers(1, &id);
  const bool writable = (desc.memory.requiredAccess & MemoryAccess::CPUWrite) !=
                        MemoryAccess::None;
  const bool readable = (desc.memory.requiredAccess & MemoryAccess::CPURead) !=
                        MemoryAccess::None;
  const bool hostVisible = writable || readable;
  const GLbitfield mapping = GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT |
                             (writable ? GL_MAP_WRITE_BIT : 0) |
                             (readable ? GL_MAP_READ_BIT : 0);
  glNamedBufferStorage(id, static_cast<GLsizeiptr>(desc.size), nullptr,
                       GL_DYNAMIC_STORAGE_BIT | (hostVisible ? mapping : 0));
  if (!desc.initialData.empty())
    glNamedBufferSubData(id, 0,
                         static_cast<GLsizeiptr>(desc.initialData.size()),
                         desc.initialData.data());
  const auto handle =
      MakeHandle<BufferHandle>(m_impl->nextBuffer++, DeviceId());
  m_impl->resources.buffers.emplace(
      handle,
      Impl::BufferResource{id,
                           BufferMetadata{desc.size, desc.usage, desc.memory},
                           m_impl->Keep([id] { glDeleteBuffers(1, &id); })});
  return handle;
}
std::span<std::byte> OpenGLRenderDevice::MapMemory(BufferHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  const auto& b = m_impl->resources.buffers.at(h);
  const auto access = b.metadata.memory.requiredAccess;
  const GLbitfield flags =
      GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT |
      ((access & MemoryAccess::CPUWrite) != MemoryAccess::None
           ? GL_MAP_WRITE_BIT
           : 0) |
      ((access & MemoryAccess::CPURead) != MemoryAccess::None ? GL_MAP_READ_BIT
                                                              : 0);
  auto* mapped = glMapNamedBufferRange(b.id, 0, b.metadata.size, flags);
  return {static_cast<std::byte*>(mapped), size_t(b.metadata.size)};
}
void OpenGLRenderDevice::UnmapMemory(BufferHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  glUnmapNamedBuffer(m_impl->resources.buffers.at(h).id);
}
void OpenGLRenderDevice::FlushMappedMemoryRange(BufferHandle, std::uint64_t,
                                                std::uint64_t)
{
  Impl::Scope resourceScope(*m_impl);
  // GL_MAP_COHERENT_BIT makes host writes visible to subsequent commands.
}
void OpenGLRenderDevice::InvalidateMappedMemoryRange(BufferHandle,
                                                     std::uint64_t,
                                                     std::uint64_t)
{
  Impl::Scope resourceScope(*m_impl);
  // Coherent mappings require completion synchronization, no invalidation.
}
FenceHandle OpenGLRenderDevice::CreateFence(bool signaled)
{
  std::lock_guard lock(m_impl->syncMutex);
  const auto handle = MakeHandle<FenceHandle>(m_impl->nextFence++, DeviceId());
  auto fence = std::make_shared<Impl::Fence>();
  fence->initiallySignaled = signaled;
  m_impl->fences.emplace(handle, std::move(fence));
  return handle;
}
void OpenGLRenderDevice::DestroyFence(FenceHandle handle)
{
  std::lock_guard lock(m_impl->syncMutex);
  m_impl->fences.erase(handle);
}
void OpenGLRenderDevice::ResetFence(FenceHandle handle)
{
  auto fresh = std::make_shared<Impl::Fence>();
  std::lock_guard lock(m_impl->syncMutex);
  m_impl->fences.at(handle) = std::move(fresh);
}
bool OpenGLRenderDevice::GetFenceStatus(FenceHandle handle)
{
  return WaitForFence(handle, 0);
}
bool OpenGLRenderDevice::WaitForFence(FenceHandle handle, std::uint64_t timeout)
{
  std::shared_ptr<Impl::Fence> fence;
  {
    std::lock_guard lock(m_impl->syncMutex);
    fence = m_impl->fences.at(handle);
  }
  if (fence->initiallySignaled)
    return true;
  const auto start = std::chrono::steady_clock::now();
  Impl::SyncPtr sync;
  {
    std::unique_lock lock(fence->point->mutex);
    if (timeout == UINT64_MAX)
      fence->point->issued.wait(lock, [&] { return fence->point->published; });
    else if (!fence->point->issued.wait_for(
                 lock,
                 std::chrono::nanoseconds(
                     std::min(timeout, std::uint64_t(INT64_MAX))),
                 [&] { return fence->point->published; }))
      return false;
    sync = fence->point->sync;
  }
  if (sync)
  {
    Impl::WaitScope context(*m_impl);
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    const auto remaining = timeout == UINT64_MAX ? timeout
                           : timeout > elapsed   ? timeout - elapsed
                                                 : 0;
    const auto status = glClientWaitSync(sync->native, 0, remaining);
    if (status == GL_WAIT_FAILED)
      throw std::runtime_error("OpenGL host fence wait failed");
    if (status == GL_TIMEOUT_EXPIRED)
      return false;
  }
  std::exception_ptr failure;
  {
    std::lock_guard lock(fence->point->mutex);
    failure = std::exchange(fence->point->failure, {});
  }
  for (auto& queue : m_queues)
    static_cast<Queue&>(*queue).CollectLater();
  if (failure)
    std::rethrow_exception(failure);
  return true;
}
SemaphoreHandle OpenGLRenderDevice::CreateSemaphore(SemaphoreType type,
                                                    std::uint64_t initialValue)
{
  auto semaphore = std::make_shared<Impl::Semaphore>();
  semaphore->type = type;
  semaphore->counter = semaphore->lastSignal = initialValue;
  std::lock_guard lock(m_impl->syncMutex);
  const auto handle =
      MakeHandle<SemaphoreHandle>(m_impl->nextSemaphore++, DeviceId());
  m_impl->semaphores.emplace(handle, std::move(semaphore));
  return handle;
}
void OpenGLRenderDevice::DestroySemaphore(SemaphoreHandle handle)
{
  std::lock_guard lock(m_impl->syncMutex);
  m_impl->semaphores.erase(handle);
}
std::uint64_t
OpenGLRenderDevice::GetSemaphoreCounterValue(SemaphoreHandle handle)
{
  std::shared_ptr<Impl::Semaphore> semaphore;
  std::vector<std::pair<std::uint64_t, std::shared_ptr<Impl::Point>>> points;
  {
    std::lock_guard lock(m_impl->syncMutex);
    semaphore = m_impl->semaphores.at(handle);
    if (semaphore->type != SemaphoreType::Timeline)
      throw std::logic_error("Binary semaphores have no host counter");
    points.assign(semaphore->signals.begin(), semaphore->signals.end());
  }
  bool completed = false;
  Impl::WaitScope context(*m_impl);
  for (const auto& [value, point] : points)
  {
    Impl::SyncPtr sync;
    {
      std::lock_guard lock(point->mutex);
      if (!point->published)
        break;
      if (point->failure)
        std::rethrow_exception(point->failure);
      sync = point->sync;
    }
    if (sync)
    {
      const auto result = glClientWaitSync(sync->native, 0, 0);
      if (result == GL_WAIT_FAILED)
        throw std::runtime_error("OpenGL timeline query failed");
      if (result == GL_TIMEOUT_EXPIRED)
        break;
    }
    std::lock_guard lock(m_impl->syncMutex);
    semaphore->counter = std::max(semaphore->counter, value);
    semaphore->signals.erase(value);
    completed = true;
  }
  std::uint64_t value;
  {
    std::lock_guard lock(m_impl->syncMutex);
    value = semaphore->counter;
  }
  if (completed)
    for (auto& queue : m_queues)
      static_cast<Queue&>(*queue).CollectLater();
  return value;
}
void OpenGLRenderDevice::SignalSemaphore(SemaphoreHandle handle,
                                         std::uint64_t value)
{
  GetSemaphoreCounterValue(handle);
  std::lock_guard lock(m_impl->syncMutex);
  auto semaphore = m_impl->semaphores.at(handle);
  if (!semaphore->signals.empty())
    throw std::logic_error(
        "Host signaling while GPU timeline signals are pending is unsupported");
  if (value <= std::max(semaphore->counter, semaphore->lastSignal))
    throw std::logic_error("Timeline signal values must increase");
  semaphore->counter = semaphore->lastSignal = value;
}
bool OpenGLRenderDevice::WaitSemaphore(SemaphoreHandle handle,
                                       std::uint64_t value,
                                       std::uint64_t timeout)
{
  if (GetSemaphoreCounterValue(handle) >= value)
    return true;
  std::shared_ptr<Impl::Point> point;
  {
    std::lock_guard lock(m_impl->syncMutex);
    const auto semaphore = m_impl->semaphores.at(handle);
    const auto found = semaphore->signals.lower_bound(value);
    if (found == semaphore->signals.end())
    {
      if (!timeout)
        return false;
      throw std::logic_error("GLsync fallback cannot wait on an unsubmitted "
                             "future timeline value");
    }
    point = found->second;
  }
  const auto start = std::chrono::steady_clock::now();
  Impl::SyncPtr sync;
  {
    std::unique_lock lock(point->mutex);
    if (timeout == UINT64_MAX)
      point->issued.wait(lock, [&] { return point->published; });
    else if (!point->issued.wait_for(lock,
                                     std::chrono::nanoseconds(std::min(
                                         timeout, std::uint64_t(INT64_MAX))),
                                     [&] { return point->published; }))
      return false;
    if (point->failure)
      std::rethrow_exception(point->failure);
    sync = point->sync;
  }
  if (sync)
  {
    Impl::WaitScope context(*m_impl);
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    const auto remaining = timeout == UINT64_MAX ? timeout
                           : timeout > elapsed   ? timeout - elapsed
                                                 : 0;
    const auto status = glClientWaitSync(sync->native, 0, remaining);
    if (status == GL_WAIT_FAILED)
      throw std::runtime_error("OpenGL timeline host wait failed");
    if (status == GL_TIMEOUT_EXPIRED)
      return false;
  }
  GetSemaphoreCounterValue(handle);
  return true;
}

std::optional<BufferMetadata>
OpenGLRenderDevice::GetBufferDesc(BufferHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  const auto it = m_impl->resources.buffers.find(handle);
  if (it == m_impl->resources.buffers.end())
    return std::nullopt;
  return it->second.metadata;
}
bool OpenGLRenderDevice::IsValid(BufferHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  return m_impl->resources.buffers.contains(handle);
}
bool OpenGLRenderDevice::IsValid(
    DescriptorSetLayoutHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  return m_impl->resources.setLayouts.contains(handle);
}
bool OpenGLRenderDevice::IsValid(DescriptorSetHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  return m_impl->resources.sets.contains(handle);
}
bool OpenGLRenderDevice::IsValid(PipelineLayoutHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  return m_impl->resources.pipelineLayouts.contains(handle);
}
bool OpenGLRenderDevice::IsValid(SamplerHandle handle) const noexcept
{
  Impl::ReadScope metadataRead(*m_impl);
  return m_impl->resources.samplers.contains(handle);
}

DescriptorSetLayoutHandle OpenGLRenderDevice::CreateDescriptorSetLayout(
    const DescriptorSetLayoutDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
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
  m_impl->resources.setLayouts.emplace(handle, std::move(layout));
  return handle;
}

DescriptorSetHandle
OpenGLRenderDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
  const auto layout = m_impl->resources.setLayouts.find(desc.layout);
  if (layout == m_impl->resources.setLayouts.end())
    throw std::invalid_argument(
        "Descriptor set layout is invalid or belongs to another device");
  Impl::DescriptorSet set;
  set.layout = desc.layout;
  for (const auto& [binding, info] : layout->second.bindings)
    set.resources.emplace(binding,
                          std::vector<Impl::StoredResource>(info.count));
  const auto handle =
      MakeHandle<DescriptorSetHandle>(m_impl->nextSet++, DeviceId());
  m_impl->resources.sets.emplace(handle, std::move(set));
  return handle;
}

PipelineLayoutHandle
OpenGLRenderDevice::CreatePipelineLayout(const PipelineLayoutDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
  Impl::PipelineLayout layout;
  layout.sets = desc.sets;
  std::uint32_t ubo{}, ssbo{}, texture{};
  for (std::uint32_t setIndex = 0; setIndex < desc.sets.size(); ++setIndex)
  {
    const auto source = m_impl->resources.setLayouts.find(desc.sets[setIndex]);
    if (source == m_impl->resources.setLayouts.end())
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
  m_impl->resources.pipelineLayouts.emplace(handle, std::move(layout));
  return handle;
}

SamplerHandle OpenGLRenderDevice::CreateSampler(const SamplerDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
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
  m_impl->resources.samplers.emplace(handle, sampler);
  m_impl->resources.samplerLifetimes.emplace(
      handle, m_impl->Keep([sampler] { glDeleteSamplers(1, &sampler); }));
  return handle;
}

void OpenGLRenderDevice::UpdateDescriptorSet(
    DescriptorSetHandle handle, std::span<const DescriptorWrite> writes)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  auto found = m_impl->resources.sets.find(handle);
  if (found == m_impl->resources.sets.end())
    throw std::invalid_argument("Invalid descriptor set");
  auto updated = found->second.resources;
  const auto layout = m_impl->resources.setLayouts.find(found->second.layout);
  if (layout == m_impl->resources.setLayouts.end())
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
      const auto buffer = m_impl->resources.buffers.find(info.buffer);
      if (buffer == m_impl->resources.buffers.end() ||
          info.buffer.owner != DeviceId())
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
      const auto view = m_impl->resources.views.find(info.image);
      if (view == m_impl->resources.views.end() ||
          info.image.owner != DeviceId() ||
          !m_impl->resources.samplers.contains(info.sampler) ||
          info.sampler.owner != DeviceId())
        throw std::invalid_argument(
            "Invalid or cross-device descriptor image or sampler");
      const auto image = m_impl->resources.images.find(view->second.image);
      if (image == m_impl->resources.images.end() ||
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
  Impl::Scope resourceScope(*m_impl);
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
  m_impl->resources.shaders.emplace(handle,
                                    Impl::Shader{desc.stage,
                                                 std::string(desc.entryPoint),
                                                 {words.begin(), words.end()}});
  return handle;
}

GraphicsPipelineHandle
OpenGLRenderDevice::CreatePipeline(const GraphicsPipelineDesc& desc)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  std::vector<const Impl::Shader*> shaderModules;
  if (desc.computeShader)
  {
    const auto it = m_impl->resources.shaders.find(desc.computeShader);
    if (it == m_impl->resources.shaders.end() ||
        it->second.stage != ShaderStageFlags::Compute)
      throw std::invalid_argument("Pipeline requires a compute shader");
    shaderModules.push_back(&it->second);
  }
  else
  {
    const auto vert = m_impl->resources.shaders.find(desc.vertexShader),
               frag = m_impl->resources.shaders.find(desc.fragmentShader);
    if (vert == m_impl->resources.shaders.end() ||
        frag == m_impl->resources.shaders.end() ||
        vert->second.stage != ShaderStageFlags::Vertex ||
        frag->second.stage != ShaderStageFlags::Fragment)
      throw std::invalid_argument(
          "Pipeline requires vertex and fragment shaders");
    shaderModules = {&vert->second, &frag->second};
  }
  const auto layoutIt = m_impl->resources.pipelineLayouts.find(desc.layout);
  if (!desc.layout || layoutIt == m_impl->resources.pipelineLayouts.end())
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
  pipeline.lifetime = m_impl->Keep([program] { glDeleteProgram(program); });
  pipeline.topology = desc.topology;
  pipeline.blend = desc.blend;
  m_impl->resources.pipelines.emplace(handle, std::move(pipeline));
  return handle;
}

std::unique_ptr<IRenderCommandList>
OpenGLRenderDevice::CreateCommandList(std::uint32_t queueIndex)
{
  if (!GetQueue(queueIndex))
    throw std::out_of_range("Queue index is unavailable");
  return std::make_unique<OpenGLCommandList>(DeviceId());
}
void OpenGLRenderDevice::DestroyImage(ImageHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  auto i = m_impl->resources.images.find(h);
  if (i != m_impl->resources.images.end())
  {
    m_impl->resources.images.erase(i);
    for (auto view = m_impl->resources.views.begin();
         view != m_impl->resources.views.end();)
      if (view->second.image == h)
      {
        const auto viewHandle = view->first;
        view = m_impl->resources.views.erase(view);
        for (auto& [_, set] : m_impl->resources.sets)
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
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  auto i = m_impl->resources.views.find(h);
  if (i != m_impl->resources.views.end())
  {
    m_impl->resources.views.erase(i);
    for (auto& [_, set] : m_impl->resources.sets)
      for (auto& [__, values] : set.resources)
        for (auto& value : values)
          if (auto info = std::get_if<DescriptorImageInfo>(&value);
              info && info->image == h)
            value = std::monostate{};
  }
}
void OpenGLRenderDevice::DestroyBuffer(BufferHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->ActivateContext();
  auto i = m_impl->resources.buffers.find(h);
  if (i != m_impl->resources.buffers.end())
  {
    m_impl->resources.buffers.erase(i);
    for (auto& [_, set] : m_impl->resources.sets)
      for (auto& [__, values] : set.resources)
        for (auto& value : values)
          if (auto info = std::get_if<DescriptorBufferInfo>(&value);
              info && info->buffer == h)
            value = std::monostate{};
  }
}
void OpenGLRenderDevice::DestroyPipeline(GraphicsPipelineHandle h)
{
  Impl::Scope scope(*m_impl);
  m_impl->resources.pipelines.erase(h);
  for (auto& queue : m_queues)
    static_cast<Queue&>(*queue).RetirePipeline(h);
}
void OpenGLRenderDevice::DestroyModule(ShaderModuleHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->resources.shaders.erase(h);
}
void OpenGLRenderDevice::DestroyDescriptorSet(DescriptorSetHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  m_impl->resources.sets.erase(h);
  for (auto it = m_impl->State().boundSets.begin();
       it != m_impl->State().boundSets.end();)
    if (it->second == h)
      it = m_impl->State().boundSets.erase(it);
    else
      ++it;
}
void OpenGLRenderDevice::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  if (!m_impl->resources.setLayouts.contains(h))
    return;
  for (auto it = m_impl->resources.sets.begin();
       it != m_impl->resources.sets.end();)
    if (it->second.layout == h)
      it = m_impl->resources.sets.erase(it);
    else
      ++it;
  for (auto it = m_impl->resources.pipelineLayouts.begin();
       it != m_impl->resources.pipelineLayouts.end();)
  {
    if (std::find(it->second.sets.begin(), it->second.sets.end(), h) !=
        it->second.sets.end())
    {
      const auto dead = it->first;
      for (auto p = m_impl->resources.pipelines.begin();
           p != m_impl->resources.pipelines.end();)
        if (p->second.pipelineLayout == dead)
        {
          DestroyPipeline(p->first);
          p = m_impl->resources.pipelines.begin();
        }
        else
          ++p;
      it = m_impl->resources.pipelineLayouts.erase(it);
    }
    else
      ++it;
  }
  m_impl->resources.setLayouts.erase(h);
}
void OpenGLRenderDevice::DestroyPipelineLayout(PipelineLayoutHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  if (!m_impl->resources.pipelineLayouts.contains(h))
    return;
  for (auto p = m_impl->resources.pipelines.begin();
       p != m_impl->resources.pipelines.end();)
    if (p->second.pipelineLayout == h)
    {
      const auto pipeline = p->first;
      ++p;
      DestroyPipeline(pipeline);
    }
    else
      ++p;
  m_impl->resources.pipelineLayouts.erase(h);
}
void OpenGLRenderDevice::DestroySampler(SamplerHandle h)
{
  Impl::Scope resourceScope(*m_impl);
  const auto i = m_impl->resources.samplers.find(h);
  if (i == m_impl->resources.samplers.end())
    return;
  m_impl->ActivateContext();
  m_impl->resources.samplerLifetimes.erase(h);
  m_impl->resources.samplers.erase(i);
  for (auto& [_, set] : m_impl->resources.sets)
    for (auto& [__, values] : set.resources)
      for (auto& value : values)
        if (auto info = std::get_if<DescriptorImageInfo>(&value);
            info && info->sampler == h)
          value = std::monostate{};
}
RenderCapabilities OpenGLRenderDevice::GetCapabilities() const
{
  Impl::Scope resourceScope(*m_impl);
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
  Impl::ReadScope metadataRead(*m_impl);
  auto* target = m_impl->FindSwapchain(swapchain);
  if (!target)
    return {};
  std::lock_guard targetLock(target->mutex);
  return target->acquired ? target->images[target->imageIndex].view
                          : ImageViewHandle{};
}

void OpenGLRenderDevice::Queue::Submit(const QueueSubmitInfo& submission,
                                       FenceHandle fence)
{
  std::vector<std::vector<OpenGLCommandList::Command>> commands;
  for (const auto* list : submission.commandLists)
  {
    const auto* native = dynamic_cast<const OpenGLCommandList*>(list);
    if (!native || native->Owner() != device.DeviceId())
      throw std::invalid_argument("Cross-device OpenGL command list");
    if (native->IsRecording())
      throw std::logic_error("End command recording before submission");
    commands.push_back(native->Commands());
  }
  auto snapshot = device.m_impl->Capture();
  std::vector<std::shared_ptr<Impl::Point>> waits, signals;
  std::shared_ptr<Impl::Point> completion;
  std::lock_guard syncLock(device.m_impl->syncMutex);
  std::vector<std::shared_ptr<Impl::Semaphore>> consumed;
  struct Signal
  {
    std::shared_ptr<Impl::Semaphore> semaphore;
    std::uint64_t value;
    std::shared_ptr<Impl::Point> point;
  };
  std::vector<Signal> reservations;
  for (const auto& wait : submission.waits)
  {
    const auto semaphore = device.m_impl->semaphores.at(wait.semaphore);
    if (semaphore->type == SemaphoreType::Timeline &&
        semaphore->counter >= wait.value)
      continue;
    const auto it = semaphore->type == SemaphoreType::Timeline
                        ? semaphore->signals.lower_bound(wait.value)
                        : semaphore->signals.begin();
    if (it == semaphore->signals.end())
      throw std::logic_error(
          "OpenGL GLsync fallback cannot wait on a signal not yet submitted; "
          "external semaphore backing is unavailable");
    if (semaphore->type == SemaphoreType::Binary)
    {
      if (std::find(consumed.begin(), consumed.end(), semaphore) !=
          consumed.end())
        throw std::logic_error("Binary semaphore wait consumed twice");
      consumed.push_back(semaphore);
    }
    waits.push_back(it->second);
  }
  for (const auto& signal : submission.signals)
  {
    auto semaphore = device.m_impl->semaphores.at(signal.semaphore);
    if (std::any_of(reservations.begin(), reservations.end(),
                    [&](const Signal& value)
                    { return value.semaphore == semaphore; }))
      throw std::logic_error("Duplicate semaphore signal in submission");
    if (semaphore->type == SemaphoreType::Timeline)
    {
      if (signal.value <= std::max(semaphore->counter, semaphore->lastSignal))
        throw std::logic_error("Timeline signal values must increase");
    }
    else if (!semaphore->signals.empty() &&
             std::find(consumed.begin(), consumed.end(), semaphore) ==
                 consumed.end())
    {
      throw std::logic_error(
          "Binary semaphore must be consumed before signaling again");
    }
    auto point = std::make_shared<Impl::Point>();
    signals.push_back(point);
    reservations.push_back(
        {semaphore,
         semaphore->type == SemaphoreType::Timeline ? signal.value : 0, point});
  }
  if (fence)
  {
    const auto state = device.m_impl->fences.at(fence);
    completion = state->point;
    if (state->initiallySignaled || completion->reserved)
      throw std::logic_error("Reset a fence before reusing it");
  }
  for (const auto& semaphore : consumed)
    semaphore->signals.clear();
  for (const auto& signal : reservations)
  {
    signal.semaphore->signals.emplace(signal.value, signal.point);
    signal.semaphore->lastSignal = signal.value;
  }
  if (completion)
    completion->reserved = true;
  try
  {
    worker.Post(
        [this, commands = std::move(commands), snapshot = std::move(snapshot),
         waits, signals, completion]
        {
          Impl::SyncPtr finished;
          std::exception_ptr error;
          execution.snapshot = snapshot.get();
          state.submissionWrites.clear();
          try
          {
            worker.RethrowFailure();
            Collect();
            if (snapshot->resources.publication)
              glWaitSync(snapshot->resources.publication->native, 0,
                         GL_TIMEOUT_IGNORED);
            for (const auto& wait : waits)
              WaitPoint(wait);
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
            for (const auto& list : commands)
              for (const auto& command : list)
                std::visit([this](const auto& value)
                           { value.Execute(&device); }, command);
          }
          catch (...)
          {
            error = std::current_exception();
          }
          execution.snapshot = nullptr;
          if (error && state.activeFramebuffer)
          {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &state.activeFramebuffer);
            state.activeFramebuffer = 0;
          }
          for (const auto& frame : snapshot->frames)
          {
            if (std::find(state.submissionWrites.begin(),
                          state.submissionWrites.end(),
                          frame.handle) == state.submissionWrites.end())
              continue;
            std::lock_guard frameLock(frame.target->mutex);
            if (!frame.target->dead && frame.target->acquired &&
                frame.target->images.at(frame.target->imageIndex).image ==
                    frame.image)
              frame.target->frameReady = !error;
          }
          state.submissionWrites.clear();
          try
          {
            finished = device.m_impl->InsertSync();
            flights.push_back({finished, snapshot});
          }
          catch (...)
          {
            if (!error)
              error = std::current_exception();
          }
          if (error && !pendingFailure)
            pendingFailure = error;
          for (const auto& signal : signals)
            signal->Publish(finished, error ? error : pendingFailure);
          if (completion)
          {
            completion->Publish(finished, pendingFailure);
            pendingFailure = {};
          }
        });
  }
  catch (...)
  {
    auto error = std::current_exception();
    for (const auto& signal : signals)
      signal->Publish({}, error);
    if (completion)
      completion->Publish({}, error);
    throw;
  }
}

void OpenGLRenderDevice::Queue::Present(
    SwapchainHandle swapchain, std::span<const SemaphoreHandle> handles)
{
  auto snapshot = device.m_impl->Capture();
  auto target = device.m_impl->ShareSwapchain(swapchain);
  if (!target)
    throw std::invalid_argument("OpenGL present received an invalid swapchain");
  if (presentSurface && target->surfaceHandle != presentSurface)
    throw std::invalid_argument(
        "Queue was created for another presentation surface");
  std::vector<std::shared_ptr<Impl::Point>> waits;
  {
    std::lock_guard syncLock(device.m_impl->syncMutex);
    std::vector<std::shared_ptr<Impl::Semaphore>> consumed;
    for (auto handle : handles)
    {
      auto semaphore = device.m_impl->semaphores.at(handle);
      if (semaphore->type != SemaphoreType::Binary ||
          semaphore->signals.empty() ||
          std::find(consumed.begin(), consumed.end(), semaphore) !=
              consumed.end())
        throw std::logic_error(
            "OpenGL present requires previously submitted binary signals");
      waits.push_back(semaphore->signals.begin()->second);
      consumed.push_back(semaphore);
    }
    for (const auto& semaphore : consumed)
      semaphore->signals.clear();
  }
  worker.Invoke(
      [this, swapchain, target, snapshot, waits]
      {
        worker.RethrowFailure();
        Collect();
        for (const auto& wait : waits)
          WaitPoint(wait);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        std::lock_guard targetLock(target->mutex);
        if (target->dead || !target->acquired)
          throw std::logic_error("Present requires an acquired swapchain");
        if (!target->frameReady)
          throw std::logic_error(
              "Present requires submitted writes to this swapchain");
        // Only this worker's context is used. Configless contexts accept any
        // EGL surface on this display; otherwise eglMakeCurrent checks
        // compatibility.
        if (!eglMakeCurrent(device.m_impl->group->display, target->surface,
                            target->surface, worker.Native()))
          throw std::runtime_error(
              "Queue context is incompatible with the presentation surface");
        if (!eglSwapInterval(device.m_impl->group->display,
                             target->vsync ? 1 : 0))
          throw std::runtime_error("Cannot set the presentation swap interval");
        if (snapshot->resources.publication)
          glWaitSync(snapshot->resources.publication->native, 0,
                     GL_TIMEOUT_IGNORED);
        const auto& image = target->images.at(target->imageIndex);
        GLint oldRead{}, oldDraw{};
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLuint framebuffer{};
        glCreateFramebuffers(1, &framebuffer);
        struct RestoreFramebuffer
        {
          GLuint framebuffer;
          GLint read, draw;
          bool scissor;
          ~RestoreFramebuffer()
          {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
            if (scissor)
              glEnable(GL_SCISSOR_TEST);
            glDeleteFramebuffers(1, &framebuffer);
          }
        } restore{framebuffer, oldRead, oldDraw, scissor};
        glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0,
                                  snapshot->resources.views.at(image.view).id,
                                  0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
        glDisable(GL_SCISSOR_TEST);
        EGLint width{}, height{};
        const bool extent =
            eglQuerySurface(device.m_impl->group->display, target->surface,
                            EGL_WIDTH, &width) &&
            eglQuerySurface(device.m_impl->group->display, target->surface,
                            EGL_HEIGHT, &height);
        if (!extent)
          throw std::runtime_error("Cannot query presentation extent");
        glBlitFramebuffer(0, 0, target->width, target->height, 0, 0, width,
                          height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        const bool presented = eglSwapBuffers(device.m_impl->group->display,
                                              target->surface) == EGL_TRUE;
        auto finished = device.m_impl->InsertSync();
        flights.push_back({finished, snapshot});
        // Release the surface before a different queue or resource thread uses
        // it.
        worker.MakeCurrent();
        if (!presented)
          throw std::runtime_error("EGL presentation failed");
        target->imageIndex = (target->imageIndex + 1) % target->images.size();
        target->acquired = false;
        target->frameReady = false;
        if (state.activeSwapchain == swapchain)
          state.activeSwapchain.reset();
      });
}
void OpenGLRenderDevice::WaitIdle()
{
  std::exception_ptr failure;
  for (auto& queue : m_queues)
  {
    try
    {
      queue->WaitIdle();
    }
    catch (...)
    {
      if (!failure)
        failure = std::current_exception();
    }
  }
  {
    Impl::Scope context(*m_impl, false);
    m_impl->deletions->Drain();
  }
  if (failure)
    std::rethrow_exception(failure);
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
