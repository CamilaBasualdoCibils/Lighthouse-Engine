#pragma once
#include "OpenGLDeviceGroup.hpp"
#include <Lighthouse/Backends/Render/OpenGL/OpenGLRenderDevice.hpp>
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct GLFWwindow;
struct ImGuiContext;
namespace Lighthouse
{
template <typename H> struct OpenGLHandleHash
{
  size_t operator()(H h) const noexcept
  {
    return h.index;
  }
};
struct OpenGLRenderDevice::Impl
{
  template <class H, class T>
  using Map = std::unordered_map<H, T, OpenGLHandleHash<H>>;
  struct Deletions
  {
    std::mutex mutex;
    std::vector<std::function<void()>> pending;
    void Push(std::function<void()> fn)
    {
      std::lock_guard lock(mutex);
      pending.push_back(std::move(fn));
    }
    void Drain()
    {
      std::vector<std::function<void()>> work;
      {
        std::lock_guard lock(mutex);
        work.swap(pending);
      }
      for (auto& fn : work)
        fn();
    }
  };
  struct Lifetime
  {
    std::shared_ptr<Deletions> deletions;
    std::function<void()> destroy;
    ~Lifetime()
    {
      if (destroy)
        deletions->Push(std::move(destroy));
    }
  };
  using Token = std::shared_ptr<Lifetime>;
  struct Sync
  {
    GLsync native{};
    std::shared_ptr<Deletions> deletions;
    ~Sync()
    {
      if (native)
      {
        auto value = native;
        deletions->Push([value] { glDeleteSync(value); });
      }
    }
  };
  using SyncPtr = std::shared_ptr<Sync>;
  struct Point
  {
    std::mutex mutex;
    std::condition_variable issued;
    bool published{};
    bool reserved{};
    SyncPtr sync;
    std::exception_ptr failure;
    void Publish(SyncPtr value, std::exception_ptr error = {})
    {
      {
        std::lock_guard lock(mutex);
        sync = std::move(value);
        failure = error;
        published = true;
      }
      issued.notify_all();
    }
    SyncPtr AwaitIssue()
    {
      std::unique_lock lock(mutex);
      issued.wait(lock, [this] { return published; });
      if (failure)
        std::rethrow_exception(failure);
      return sync;
    }
  };
  struct Fence
  {
    std::shared_ptr<Point> point = std::make_shared<Point>();
    bool initiallySignaled{};
  };
  struct Semaphore
  {
    SemaphoreType type;
    std::uint64_t counter{}, lastSignal{};
    std::map<std::uint64_t, std::shared_ptr<Point>> signals;
  };
  std::mutex syncMutex;
  Map<FenceHandle, std::shared_ptr<Fence>> fences;
  Map<SemaphoreHandle, std::shared_ptr<Semaphore>> semaphores;
  unsigned nextFence = 1, nextSemaphore = 1;

  struct BufferResource
  {
    GLuint id{};
    BufferMetadata metadata;
    Token lifetime;
  };
  struct ImageResource
  {
    GLuint id{};
    Flags<ImageUsage> usage;
    ImageType type{ImageType::Image2D};
    ImageFormat format{ImageFormat::RGBA8};
    Token lifetime;
  };
  struct ImageViewResource
  {
    GLuint id{};
    ImageHandle image;
    Token lifetime;
  };
  struct Shader
  {
    ShaderStageFlags stage{};
    std::string entryPoint;
    std::vector<std::uint32_t> words;
  };
  struct DescriptorSetLayout
  {
    std::map<std::uint32_t, DescriptorBindingDesc> bindings;
  };
  using StoredResource =
      std::variant<std::monostate, DescriptorBufferInfo, DescriptorImageInfo>;
  struct DescriptorSet
  {
    DescriptorSetLayoutHandle layout;
    std::map<std::uint32_t, std::vector<StoredResource>> resources;
  };
  struct PipelineLayout
  {
    std::vector<DescriptorSetLayoutHandle> sets;
    DescriptorBindingLocations locations;
  };
  struct Pipeline
  {
    GLuint program{};
    VertexLayout layout;
    PipelineLayoutHandle pipelineLayout;
    Token lifetime;
    PrimitiveTopology topology{PrimitiveTopology::TriangleList};
    BlendState blend;
  };
  struct ResourceTables
  {
    Map<ImageHandle, ImageResource> images;
    Map<ImageViewHandle, ImageViewResource> views;
    Map<BufferHandle, BufferResource> buffers;
    Map<ShaderModuleHandle, Shader> shaders;
    Map<DescriptorSetLayoutHandle, DescriptorSetLayout> setLayouts;
    Map<DescriptorSetHandle, DescriptorSet> sets;
    Map<PipelineLayoutHandle, PipelineLayout> pipelineLayouts;
    Map<SamplerHandle, GLuint> samplers;
    Map<SamplerHandle, Token> samplerLifetimes;
    Map<GraphicsPipelineHandle, Pipeline> pipelines;
    SyncPtr publication;
  } resources;
  struct SwapchainImage
  {
    ImageHandle image;
    ImageViewHandle view;
  };
  struct Swapchain
  {
    std::mutex mutex;
    bool dead{};
    SurfaceHandle surfaceHandle;
    EGLSurface surface{EGL_NO_SURFACE};
    bool vsync{true};
    void* nativeWindow{};
    std::vector<SwapchainFormat> availableFormats;
    SwapchainInfo info{SwapchainFormat::SRGB, ImageFormat::RGBA8};
    std::vector<SwapchainImage> images;
    std::uint32_t imageIndex{};
    GLsizei width{}, height{};
    bool acquired{}, frameReady{};
  };
  struct SwapchainSlot
  {
    std::uint32_t generation{1};
    std::shared_ptr<Swapchain> value;
  };
  struct Frame
  {
    SwapchainHandle handle;
    std::shared_ptr<Swapchain> target;
    ImageHandle image;
    ImageViewHandle view;
  };
  struct Snapshot
  {
    ResourceTables resources;
    std::vector<Frame> frames;
  };
  struct ContextState
  {
    GLuint activeFramebuffer{};
    std::optional<SwapchainHandle> activeSwapchain;
    std::vector<SwapchainHandle> submissionWrites;
    std::optional<GraphicsPipelineHandle> activePipeline;
    std::map<std::uint32_t, DescriptorSetHandle> boundSets;
    Map<GraphicsPipelineHandle, GLuint> vaos;
    std::vector<std::string> debugGroups;
    void Clear()
    {
      if (activeFramebuffer)
        glDeleteFramebuffers(1, &activeFramebuffer);
      for (auto [_, vao] : vaos)
        glDeleteVertexArrays(1, &vao);
      vaos.clear();
      activeFramebuffer = 0;
    }
  };
  struct Execution
  {
    Impl* owner;
    ContextState* state;
    Snapshot* snapshot{};
  };
  inline static thread_local Execution* executing = nullptr;
  inline static thread_local Impl* writer = nullptr;
  mutable ContextState hostState;
  ContextState& State() const
  {
    return executing && executing->owner == this ? *executing->state
                                                 : hostState;
  }
  ResourceTables& R()
  {
    return executing && executing->owner == this && executing->snapshot
               ? executing->snapshot->resources
               : resources;
  }
  std::shared_ptr<Snapshot> Capture()
  {
    std::shared_lock lock(metadata);
    auto snapshot = std::make_shared<Snapshot>();
    if (contextLost.load())
      throw std::runtime_error("OpenGL resource context is unavailable");
    auto& copy = snapshot->resources;
    copy.images = resources.images;
    copy.views = resources.views;
    copy.buffers = resources.buffers;
    copy.setLayouts = resources.setLayouts;
    copy.sets = resources.sets;
    copy.pipelineLayouts = resources.pipelineLayouts;
    copy.samplers = resources.samplers;
    copy.samplerLifetimes = resources.samplerLifetimes;
    copy.pipelines = resources.pipelines;
    copy.publication = resources.publication;
    for (std::uint32_t i = 0; i < swapchains.size(); ++i)
    {
      auto target = swapchains[i].value;
      if (!target)
        continue;
      std::lock_guard targetLock(target->mutex);
      if (!target->dead && target->acquired)
      {
        const auto& image = target->images.at(target->imageIndex);
        snapshot->frames.push_back({{i, swapchains[i].generation, owner},
                                    target,
                                    image.image,
                                    image.view});
      }
    }
    return snapshot;
  }
  bool ActivateImage(ImageHandle image)
  {
    if (executing && executing->owner == this && executing->snapshot)
    {
      for (const auto& frame : executing->snapshot->frames)
        if (frame.image == image)
        {
          State().activeSwapchain = frame.handle;
          return true;
        }
    }
    return false;
  }
  void ActivateView(ImageViewHandle view)
  {
    const auto found = R().views.find(view);
    if (found != R().views.end())
      ActivateImage(found->second.image);
  }
  void RecordWrite(ImageHandle image)
  {
    if (!executing || !executing->snapshot)
      return;
    for (const auto& frame : executing->snapshot->frames)
      if (frame.image == image)
      {
        auto& writes = State().submissionWrites;
        if (std::find(writes.begin(), writes.end(), frame.handle) ==
            writes.end())
          writes.push_back(frame.handle);
      }
  }
  Swapchain* FindSwapchain(SwapchainHandle h) const noexcept
  {
    if (!h || h.owner != owner || h.index >= swapchains.size())
      return nullptr;
    const auto& slot = swapchains[h.index];
    return slot.generation == h.generation ? slot.value.get() : nullptr;
  }
  std::shared_ptr<Swapchain> ShareSwapchain(SwapchainHandle h)
  {
    std::shared_lock lock(metadata);
    auto* target = FindSwapchain(h);
    return target ? swapchains[h.index].value : nullptr;
  }
  void ActivateContext(const Swapchain* requested = nullptr) const
  {
    // Execution threads never borrow a resource or swapchain context.
    if (executing && executing->owner == this)
      return;
    const auto context = resourceContext;
    const auto surface = resourceSurface;
    if (eglGetCurrentContext() == context)
      return;
    GLsync handoff = nullptr;
    if (eglGetCurrentContext() != EGL_NO_CONTEXT && writer == this)
    {
      handoff = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
      glFlush();
    }
    if (!eglMakeCurrent(group->display, surface, surface, context))
      throw std::runtime_error("Cannot activate OpenGL resource context");
    if (handoff)
    {
      glWaitSync(handoff, 0, GL_TIMEOUT_IGNORED);
      glDeleteSync(handoff);
    }
  }
  struct WaitContext
  {
    EGLContext context{EGL_NO_CONTEXT};
    EGLSurface surface{EGL_NO_SURFACE};
  };
  std::mutex waitContextsMutex;
  std::unordered_map<std::thread::id, WaitContext> waitContexts;
  // Explicit host waits must not hold the resource context, which would make
  // unrelated resource operations wait for another queue's GPU work.
  struct WaitScope
  {
    Impl& d;
    EGLDisplay previousDisplay;
    EGLenum previousApi;
    EGLContext previousContext;
    EGLSurface previousRead, previousDraw;
    explicit WaitScope(Impl& device)
        : d(device), previousDisplay(eglGetCurrentDisplay()),
          previousApi(eglQueryAPI()), previousContext(eglGetCurrentContext()),
          previousRead(eglGetCurrentSurface(EGL_READ)),
          previousDraw(eglGetCurrentSurface(EGL_DRAW))
    {
      try
      {
        if (!eglBindAPI(EGL_OPENGL_API))
          throw std::runtime_error("Cannot bind host synchronization API");
        WaitContext context;
        {
          std::lock_guard lock(d.waitContextsMutex);
          auto found = d.waitContexts.find(std::this_thread::get_id());
          if (found == d.waitContexts.end())
          {
            context.context = d.group->CreateSharedContext();
            const EGLint attributes[]{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
            context.surface = eglCreatePbufferSurface(
                d.group->display, d.group->config, attributes);
            if (context.surface == EGL_NO_SURFACE)
            {
              eglDestroyContext(d.group->display, context.context);
              throw std::runtime_error(
                  "Cannot create host synchronization pbuffer");
            }
            try
            {
              found =
                  d.waitContexts.emplace(std::this_thread::get_id(), context)
                      .first;
            }
            catch (...)
            {
              eglDestroySurface(d.group->display, context.surface);
              eglDestroyContext(d.group->display, context.context);
              throw;
            }
          }
          context = found->second;
        }
        if (!eglMakeCurrent(d.group->display, context.surface, context.surface,
                            context.context))
          throw std::runtime_error(
              "Cannot activate host synchronization context");
      }
      catch (...)
      {
        Restore();
        throw;
      }
    }
    void Restore() noexcept
    {
      eglMakeCurrent(d.group->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
      eglBindAPI(previousApi);
      if (previousDisplay != EGL_NO_DISPLAY &&
          previousContext != EGL_NO_CONTEXT)
        eglMakeCurrent(previousDisplay, previousDraw, previousRead,
                       previousContext);
    }
    ~WaitScope()
    {
      Restore();
    }
  };
  struct ReadScope
  {
    std::optional<std::shared_lock<std::shared_mutex>> lock;
    explicit ReadScope(Impl& d)
    {
      if (writer != &d)
        lock.emplace(d.metadata);
    }
  };
  struct Scope
  {
    Impl& d;
    std::unique_lock<std::recursive_mutex> contextLock;
    std::optional<std::unique_lock<std::shared_mutex>> metadataLock;
    Impl* previousWriter;
    EGLDisplay display;
    EGLenum api;
    EGLContext context;
    EGLSurface read, draw;
    bool publish;
    SyncPtr publication;
    Scope(Impl& device, bool changeResources = true)
        : d(device), contextLock(d.resourceContextMutex),
          previousWriter(writer), display(eglGetCurrentDisplay()),
          api(eglQueryAPI()), context(eglGetCurrentContext()),
          read(eglGetCurrentSurface(EGL_READ)),
          draw(eglGetCurrentSurface(EGL_DRAW)),
          publish(changeResources && writer != &d)
    {
      if (d.contextLost.load())
        throw std::runtime_error("OpenGL resource context was lost");
      if (publish)
      {
        metadataLock.emplace(d.metadata);
        publication = std::make_shared<Sync>();
        publication->deletions = d.deletions;
      }
      if (!eglBindAPI(EGL_OPENGL_API))
        throw std::runtime_error("Cannot bind resource OpenGL API");
      GLsync parentHandoff = nullptr;
      if (previousWriter == &d && context != EGL_NO_CONTEXT &&
          context != d.resourceContext)
      {
        parentHandoff = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
      }
      // Do not treat an unrelated saved context as part of this device stream.
      if (!eglMakeCurrent(d.group->display, d.resourceSurface,
                          d.resourceSurface, d.resourceContext))
        throw std::runtime_error("Cannot bind resource OpenGL context");
      writer = &d;
      if (parentHandoff)
      {
        glWaitSync(parentHandoff, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(parentHandoff);
      }
      if (d.resources.publication && d.resources.publication->native)
        glWaitSync(d.resources.publication->native, 0, GL_TIMEOUT_IGNORED);
      d.deletions->Drain();
    }
    ~Scope()
    {
      GLsync parentHandoff = nullptr;
      try
      {
        d.ActivateContext();
        if (publish)
        {
          publication->native = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
          if (!publication->native)
            throw std::runtime_error("Resource publication fence failed");
          glFlush();
          d.resources.publication = std::move(publication);
        }
        d.deletions->Drain();
        if (previousWriter == &d && context != EGL_NO_CONTEXT &&
            context != d.resourceContext)
        {
          parentHandoff = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
          glFlush();
        }
      }
      catch (...)
      {
        d.contextLost.store(true);
      }
      writer = previousWriter;
      eglMakeCurrent(d.group->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT);
      eglBindAPI(api);
      if (context != EGL_NO_CONTEXT && display != EGL_NO_DISPLAY)
      {
        eglMakeCurrent(display, draw, read, context);
        if (parentHandoff)
        {
          glWaitSync(parentHandoff, 0, GL_TIMEOUT_IGNORED);
          glDeleteSync(parentHandoff);
        }
      }
      else
        eglMakeCurrent(d.group->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
    }
  };

  explicit Impl(std::shared_ptr<Detail::OpenGLDeviceGroup> g, std::uint64_t id)
      : group(std::move(g)), owner(id)
  {
    group->Initialize();
    if (!eglBindAPI(EGL_OPENGL_API))
      throw std::runtime_error("Cannot bind resource OpenGL API");
    resourceContext = group->CreateSharedContext();
    const EGLint attributes[]{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    resourceSurface =
        eglCreatePbufferSurface(group->display, group->config, attributes);
    if (resourceSurface == EGL_NO_SURFACE)
    {
      eglDestroyContext(group->display, resourceContext);
      throw std::runtime_error("Resource pbuffer creation failed");
    }
  }
  ~Impl()
  {
    for (auto [_, context] : waitContexts)
    {
      eglDestroySurface(group->display, context.surface);
      eglDestroyContext(group->display, context.context);
    }
    // Also handles a constructor that failed during queue negotiation.
    try
    {
      Scope context(*this, false);
      resources = {};
      fences.clear();
      semaphores.clear();
      deletions->Drain();
    }
    catch (...)
    {
    }
    if (resourceSurface != EGL_NO_SURFACE)
      eglDestroySurface(group->display, resourceSurface);
    if (resourceContext != EGL_NO_CONTEXT)
      eglDestroyContext(group->display, resourceContext);
  }
  Token Keep(std::function<void()> destroy)
  {
    auto token = std::make_shared<Lifetime>();
    token->deletions = deletions;
    token->destroy = std::move(destroy);
    return token;
  }
  SyncPtr InsertSync()
  {
    auto point = std::make_shared<Sync>();
    point->deletions = deletions;
    point->native = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!point->native)
      throw std::runtime_error("OpenGL fence creation failed");
    glFlush();
    return point;
  }
  void CreateSwapchainImages(Swapchain&, std::uint32_t);
  void DestroySwapchainImages(Swapchain&);
  std::shared_ptr<Detail::OpenGLDeviceGroup> group;
  std::shared_ptr<Deletions> deletions = std::make_shared<Deletions>();
  std::atomic<bool> contextLost{false};
  mutable std::shared_mutex metadata;
  std::recursive_mutex resourceContextMutex; // Resource API context only; never
                                             // acquired by queue execution.
  EGLContext resourceContext = EGL_NO_CONTEXT;
  EGLSurface resourceSurface = EGL_NO_SURFACE;
  std::uint64_t owner;
  std::vector<SwapchainSlot> swapchains;
  std::unordered_map<SurfaceHandle, Window*> surfaces;
  unsigned nextSurface = 1;
  std::uint32_t nextImage{1}, nextView{1}, nextBuffer{1}, nextShader{1},
      nextPipeline{1};
  std::uint32_t nextSetLayout{1}, nextSet{1}, nextPipelineLayout{1},
      nextSampler{1};
#ifdef LIGHTHOUSE_IMGUI
  struct ContextSnapshot
  {
    GLFWwindow* glfw{};
    EGLDisplay display{EGL_NO_DISPLAY};
    EGLContext context{EGL_NO_CONTEXT};
    EGLSurface read{EGL_NO_SURFACE}, draw{EGL_NO_SURFACE};
    static ContextSnapshot Capture();
    void Restore() const noexcept;
  };
  struct ImGuiState
  {
    GLFWwindow* host{};
    ContextSnapshot saved;
    bool frameActive{};
  };
  std::unordered_map<ImGuiContext*, ImGuiState> imgui;
#endif
};
} // namespace Lighthouse
