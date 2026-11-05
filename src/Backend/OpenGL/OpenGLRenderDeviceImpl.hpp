#pragma once

#include "OpenGLDeviceGroup.hpp"
#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#include <algorithm>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

struct GLFWwindow;
struct ImGuiContext;

namespace Lighthouse
{

template <typename HandleType> struct OpenGLHandleHash
{
  std::size_t operator()(HandleType handle) const noexcept
  {
    return handle.index;
  }
};

struct OpenGLRenderDevice::Impl
{
  explicit Impl(std::shared_ptr<Detail::OpenGLDeviceGroup> deviceGroup,
                std::uint64_t deviceId)
      : group(std::move(deviceGroup)), owner(deviceId)
  {
  }

  struct SwapchainImage
  {
    ImageHandle image;
    ImageViewHandle view;
    GLuint framebuffer{};
  };
  struct Swapchain
  {
    EGLSurface surface{EGL_NO_SURFACE};
    EGLContext context{EGL_NO_CONTEXT};
    std::vector<SwapchainImage> images;
    std::uint32_t imageIndex{};
    GLsizei width{}, height{};
    bool acquired{}, frameReady{};
  };
  struct SwapchainSlot
  {
    std::uint32_t generation{1};
    std::unique_ptr<Swapchain> value;
  };
  struct BufferResource
  {
    GLuint id{};
    BufferMetadata metadata;
  };
  struct ImageResource
  {
    GLuint id{};
    Flags<ImageUsage> usage{};
    ImageType type{ImageType::Image2D};
  };
  struct ImageViewResource
  {
    GLuint id{};
    ImageHandle image;
  };

  void CreateSwapchainImages(Swapchain& swapchain, std::uint32_t count);
  void DestroySwapchainImages(Swapchain& swapchain);

  Swapchain* FindSwapchain(SwapchainHandle handle) const noexcept
  {
    if (!handle || handle.owner != owner || handle.index >= swapchains.size())
      return nullptr;
    const auto& slot = swapchains[handle.index];
    return slot.generation == handle.generation ? slot.value.get() : nullptr;
  }

  bool ActivateImage(ImageHandle image)
  {
    for (std::uint32_t i = 0; i < swapchains.size(); ++i)
    {
      auto* target = swapchains[i].value.get();
      if (!target || !target->acquired ||
          target->images[target->imageIndex].image != image)
        continue;
      ActivateContext(target);
      activeSwapchain = SwapchainHandle{i, swapchains[i].generation, owner};
      return true;
    }
    return false;
  }
  void RecordWrite(ImageHandle image)
  {
    for (std::uint32_t i = 0; i < swapchains.size(); ++i)
    {
      const auto* target = swapchains[i].value.get();
      if (!target || !target->acquired ||
          target->images[target->imageIndex].image != image)
        continue;
      const SwapchainHandle handle{i, swapchains[i].generation, owner};
      if (std::find(submissionWrites.begin(), submissionWrites.end(), handle) ==
          submissionWrites.end())
        submissionWrites.push_back(handle);
      return;
    }
  }
  void ActivateView(ImageViewHandle view)
  {
    const auto found = views.find(view);
    if (found != views.end())
      ActivateImage(found->second.image);
  }

  void ActivateContext(const Swapchain* requested = nullptr) const
  {
    if (!group || group->display == EGL_NO_DISPLAY)
      throw std::logic_error("Initialize an EGL display by creating a "
                             "swapchain before using the OpenGL device");
    const auto context = requested ? requested->context : group->shareContext;
    const auto surface = requested ? requested->surface : group->pbuffer;
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE)
      throw std::logic_error("The OpenGL device has no active EGL context");
    const auto current = eglGetCurrentContext();
    if (current == context)
      return;
    // Shared textures may be consumed by a different window context next.
    // Complete producer work before switching contexts.
    const bool ownedContext =
        current != EGL_NO_CONTEXT &&
        (current == group->shareContext ||
         std::any_of(swapchains.begin(), swapchains.end(),
                     [current](const auto& slot)
                     { return slot.value && slot.value->context == current; }));
    if (ownedContext)
      glFinish();
    if (eglMakeCurrent(group->display, surface, surface, context) != EGL_TRUE)
      throw std::runtime_error("Failed to make the EGL OpenGL context current");
  }

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

  std::shared_ptr<Detail::OpenGLDeviceGroup> group;
  std::uint64_t owner{};
  std::vector<SwapchainSlot> swapchains;
  GLuint activeFramebuffer{};
  // Context selection is independent of each swapchain's acquisition state.
  std::optional<SwapchainHandle> activeSwapchain;
  std::vector<SwapchainHandle> submissionWrites;
  std::uint32_t nextImage{1}, nextView{1}, nextBuffer{1}, nextShader{1},
      nextPipeline{1};
  std::uint32_t nextSetLayout{1}, nextSet{1}, nextPipelineLayout{1},
      nextSampler{1};
  std::unordered_map<ImageHandle, ImageResource, OpenGLHandleHash<ImageHandle>>
      images;
  std::unordered_map<ImageViewHandle, ImageViewResource,
                     OpenGLHandleHash<ImageViewHandle>>
      views;
  std::unordered_map<BufferHandle, BufferResource,
                     OpenGLHandleHash<BufferHandle>>
      buffers;
  struct Shader
  {
    ShaderStageFlags stage{};
    std::string entryPoint;
    std::vector<std::uint32_t> words;
  };
  std::unordered_map<ShaderModuleHandle, Shader,
                     OpenGLHandleHash<ShaderModuleHandle>>
      shaders;
  struct DescriptorSetLayout
  {
    std::map<std::uint32_t, DescriptorBindingDesc> bindings;
  };
  std::unordered_map<DescriptorSetLayoutHandle, DescriptorSetLayout,
                     OpenGLHandleHash<DescriptorSetLayoutHandle>>
      setLayouts;
  using StoredResource =
      std::variant<std::monostate, DescriptorBufferInfo, DescriptorImageInfo>;
  struct DescriptorSet
  {
    DescriptorSetLayoutHandle layout;
    std::map<std::uint32_t, std::vector<StoredResource>> resources;
  };
  std::unordered_map<DescriptorSetHandle, DescriptorSet,
                     OpenGLHandleHash<DescriptorSetHandle>>
      sets;
  struct PipelineLayout
  {
    std::vector<DescriptorSetLayoutHandle> sets;
    DescriptorBindingLocations locations;
  };
  std::unordered_map<PipelineLayoutHandle, PipelineLayout,
                     OpenGLHandleHash<PipelineLayoutHandle>>
      pipelineLayouts;
  std::unordered_map<SamplerHandle, GLuint, OpenGLHandleHash<SamplerHandle>>
      samplers;
  struct Pipeline
  {
    GLuint program{};
    VertexLayout layout;
    PipelineLayoutHandle pipelineLayout;
    std::unordered_map<SwapchainHandle, GLuint,
                       OpenGLHandleHash<SwapchainHandle>>
        vaos;
  };
  std::unordered_map<GraphicsPipelineHandle, Pipeline,
                     OpenGLHandleHash<GraphicsPipelineHandle>>
      pipelines;
  std::optional<GraphicsPipelineHandle> activePipeline;
  std::map<std::uint32_t, DescriptorSetHandle> boundSets;
};

} // namespace Lighthouse
