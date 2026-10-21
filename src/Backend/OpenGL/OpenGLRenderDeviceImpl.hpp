#pragma once

#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>
#include "OpenGLDeviceGroup.hpp"

#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <EGL/egl.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace Lighthouse {

template <typename HandleType>
struct OpenGLHandleHash {
    std::size_t operator()(HandleType handle) const noexcept { return handle.index; }
};

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
    std::optional<SwapchainHandle> acquired;
    bool frameReady{};
    std::uint32_t nextImage{1}, nextView{1}, nextBuffer{1}, nextShader{1}, nextPipeline{1};
    std::unordered_map<ImageHandle, GLuint, OpenGLHandleHash<ImageHandle>> images;
    std::unordered_map<ImageViewHandle, GLuint, OpenGLHandleHash<ImageViewHandle>> views;
    std::unordered_map<BufferHandle, BufferResource, OpenGLHandleHash<BufferHandle>> buffers;
    std::unordered_map<ShaderModuleHandle, GLuint, OpenGLHandleHash<ShaderModuleHandle>> shaders;
    struct Pipeline {
        GLuint program{};
        VertexLayout layout;
        std::unordered_map<SwapchainHandle, GLuint, OpenGLHandleHash<SwapchainHandle>> vaos;
    };
    std::unordered_map<GraphicsPipelineHandle, Pipeline, OpenGLHandleHash<GraphicsPipelineHandle>> pipelines;
};

} // namespace Lighthouse
