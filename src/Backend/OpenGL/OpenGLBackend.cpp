#include <Lighthouse/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include "OpenGLDeviceGroup.hpp"

#include <EGL/eglext.h>
#include <stdexcept>

namespace Lighthouse {

namespace Detail {

OpenGLDeviceGroup::~OpenGLDeviceGroup() {
    if (display == EGL_NO_DISPLAY) return;
    if (shareContext != EGL_NO_CONTEXT) eglDestroyContext(display, shareContext);
    eglTerminate(display);
}

void OpenGLDeviceGroup::Initialize(void* nativeDisplay) {
    if (display != EGL_NO_DISPLAY) return;
    const auto candidateDisplay = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, nativeDisplay, nullptr);
    if (candidateDisplay == EGL_NO_DISPLAY || eglInitialize(candidateDisplay, nullptr, nullptr) != EGL_TRUE)
        throw std::runtime_error("Failed to initialize EGL for the window's native display");
    if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
        eglTerminate(candidateDisplay);
        throw std::runtime_error("Failed to bind the EGL OpenGL API");
    }
    const EGLint configAttributes[]{EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig candidateConfig{};
    EGLint count{};
    if (eglChooseConfig(candidateDisplay, configAttributes, &candidateConfig, 1, &count) != EGL_TRUE || !count) {
        eglTerminate(candidateDisplay);
        throw std::runtime_error("Failed to choose an EGL window configuration");
    }
    const EGLint contextAttributes[]{EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    const auto candidateContext = eglCreateContext(candidateDisplay, candidateConfig, EGL_NO_CONTEXT, contextAttributes);
    if (candidateContext == EGL_NO_CONTEXT) {
        eglTerminate(candidateDisplay);
        throw std::runtime_error("Failed to create the EGL share context");
    }
    display = candidateDisplay;
    config = candidateConfig;
    shareContext = candidateContext;
}

EGLContext OpenGLDeviceGroup::CreateSharedContext() const {
    const EGLint attributes[]{EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    const auto context = eglCreateContext(display, config, shareContext, attributes);
    if (context == EGL_NO_CONTEXT) throw std::runtime_error("Failed to create the EGL device context");
    return context;
}

EGLSurface OpenGLDeviceGroup::CreateWindowSurface(void* nativeWindow) const {
    const auto surface = eglCreateWindowSurface(display, config,
        reinterpret_cast<EGLNativeWindowType>(nativeWindow), nullptr);
    if (surface == EGL_NO_SURFACE) throw std::runtime_error("Failed to create the EGL window surface");
    return surface;
}

} // namespace Detail

struct OpenGLBackend::Impl {
    std::shared_ptr<Detail::OpenGLDeviceGroup> group{std::make_shared<Detail::OpenGLDeviceGroup>()};
};

OpenGLBackend::OpenGLBackend() : m_impl(std::make_unique<Impl>()) {}
OpenGLBackend::~OpenGLBackend() { Shutdown(); }

bool OpenGLBackend::Initialize(std::string& diagnostic) {
    diagnostic.clear();
    return true;
}

GraphicsCapabilities OpenGLBackend::Capabilities() const { return {}; }
std::unique_ptr<IRenderDevice> OpenGLBackend::CreateDevice(const DeviceConfig& config) {
    if (!m_impl->group) m_impl->group = std::make_shared<Detail::OpenGLDeviceGroup>();
    return std::make_unique<OpenGLRenderDevice>(m_impl->group, config);
}
void OpenGLBackend::Shutdown() noexcept {
    if (m_impl) m_impl->group.reset();
}

} // namespace Lighthouse
