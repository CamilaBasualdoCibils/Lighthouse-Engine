#include "OpenGLDeviceGroup.hpp"
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLRenderDevice.hpp>

#include <EGL/eglext.h>
#ifdef LIGHTHOUSE_IMGUI
#include <X11/Xlib.h>
#endif
#include <stdexcept>

namespace Lighthouse
{

namespace Detail
{

OpenGLDeviceGroup::~OpenGLDeviceGroup()
{
  if (display == EGL_NO_DISPLAY)
    return;
  if (eglGetCurrentDisplay() == display)
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  if (pbuffer != EGL_NO_SURFACE)
    eglDestroySurface(display, pbuffer);
  if (shareContext != EGL_NO_CONTEXT)
    eglDestroyContext(display, shareContext);
  eglTerminate(display);
}

void OpenGLDeviceGroup::Initialize(void* nativeDisplay)
{
  if (display != EGL_NO_DISPLAY)
    return;
  if (!nativeDisplay)
  {
    // A display connection does not require a window. Reuse GLFW's native
    // connection when one exists so later window surfaces share this device.
    try
    {
      platform = std::make_unique<WindowManager>();
      nativeDisplay = platform->NativeDisplayHandle();
    }
    catch (...)
    {
      platform.reset();
    }
  }
  EGLDisplay candidateDisplay{};
  if (nativeDisplay)
  {
#ifdef LIGHTHOUSE_IMGUI
    candidateDisplay =
        eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, nativeDisplay, nullptr);
#else
    candidateDisplay =
        eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, nativeDisplay, nullptr);
#endif
  }
  else
  {
    // EGL device displays are vendor-neutral and work without X11/Wayland.
    // Mesa's surfaceless platform is a fallback (including software rendering).
    const auto queryDevices = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        eglGetProcAddress("eglQueryDevicesEXT"));
    EGLDeviceEXT devices[32]{};
    EGLint deviceCount{};
    if (queryDevices && queryDevices(32, devices, &deviceCount))
    {
      for (EGLint i = 0; i < deviceCount; ++i)
      {
        auto candidate =
            eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[i], nullptr);
        if (candidate != EGL_NO_DISPLAY &&
            eglInitialize(candidate, nullptr, nullptr) == EGL_TRUE)
        {
          candidateDisplay = candidate;
          break;
        }
      }
    }
    if (candidateDisplay == EGL_NO_DISPLAY)
      candidateDisplay = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                               EGL_DEFAULT_DISPLAY, nullptr);
  }
  if (candidateDisplay == EGL_NO_DISPLAY ||
      eglInitialize(candidateDisplay, nullptr, nullptr) != EGL_TRUE)
    throw std::runtime_error("Failed to initialize EGL (native display, device "
                             "display, or surfaceless platform)");
  if (eglBindAPI(EGL_OPENGL_API) != EGL_TRUE)
  {
    eglTerminate(candidateDisplay);
    throw std::runtime_error("Failed to bind the EGL OpenGL API");
  }
  const EGLint configAttributes[]{
      EGL_SURFACE_TYPE,
      nativeDisplay ? EGL_WINDOW_BIT | EGL_PBUFFER_BIT : EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_BIT,
      EGL_RED_SIZE,
      8,
      EGL_GREEN_SIZE,
      8,
      EGL_BLUE_SIZE,
      8,
      EGL_ALPHA_SIZE,
      0,
#ifdef LIGHTHOUSE_IMGUI
      EGL_NATIVE_VISUAL_ID,
      nativeDisplay ? static_cast<EGLint>(XVisualIDFromVisual(DefaultVisual(
                          static_cast<Display*>(nativeDisplay),
                          DefaultScreen(static_cast<Display*>(nativeDisplay)))))
                    : EGL_DONT_CARE,
#endif
      EGL_NONE};
  EGLConfig candidateConfig{};
  EGLint count{};
  if (eglChooseConfig(candidateDisplay, configAttributes, &candidateConfig, 1,
                      &count) != EGL_TRUE ||
      !count)
  {
    eglTerminate(candidateDisplay);
    throw std::runtime_error("Failed to choose an EGL window configuration");
  }
  const EGLint contextAttributes[]{EGL_CONTEXT_MAJOR_VERSION,
                                   4,
                                   EGL_CONTEXT_MINOR_VERSION,
                                   5,
                                   EGL_CONTEXT_OPENGL_PROFILE_MASK,
                                   EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                                   EGL_NONE};
  const auto candidateContext = eglCreateContext(
      candidateDisplay, candidateConfig, EGL_NO_CONTEXT, contextAttributes);
  if (candidateContext == EGL_NO_CONTEXT)
  {
    eglTerminate(candidateDisplay);
    throw std::runtime_error("Failed to create the EGL share context");
  }
  display = candidateDisplay;
  this->nativeDisplay = nativeDisplay;
  config = candidateConfig;
  shareContext = candidateContext;
  const EGLint pbufferAttributes[]{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  pbuffer = eglCreatePbufferSurface(display, config, pbufferAttributes);
  if (pbuffer == EGL_NO_SURFACE)
  {
    eglDestroyContext(display, shareContext);
    eglTerminate(display);
    display = EGL_NO_DISPLAY;
    shareContext = EGL_NO_CONTEXT;
    throw std::runtime_error("Failed to create the EGL device pbuffer");
  }
}

EGLContext OpenGLDeviceGroup::CreateSharedContext() const
{
  const EGLint attributes[]{EGL_CONTEXT_MAJOR_VERSION,
                            4,
                            EGL_CONTEXT_MINOR_VERSION,
                            5,
                            EGL_CONTEXT_OPENGL_PROFILE_MASK,
                            EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                            EGL_NONE};
  const auto context =
      eglCreateContext(display, config, shareContext, attributes);
  if (context == EGL_NO_CONTEXT)
    throw std::runtime_error("Failed to create the EGL device context");
  return context;
}

EGLSurface OpenGLDeviceGroup::CreateWindowSurface(void* nativeWindow) const
{
  const auto surface = eglCreateWindowSurface(
      display, config, reinterpret_cast<EGLNativeWindowType>(nativeWindow),
      nullptr);
  if (surface == EGL_NO_SURFACE)
    throw std::runtime_error("Failed to create the EGL window surface");
  return surface;
}

} // namespace Detail

struct OpenGLBackend::Impl
{
  std::shared_ptr<Detail::OpenGLDeviceGroup> group{
      std::make_shared<Detail::OpenGLDeviceGroup>()};
};

OpenGLBackend::OpenGLBackend() : m_impl(std::make_unique<Impl>()) {}
OpenGLBackend::~OpenGLBackend()
{
  Shutdown();
}

bool OpenGLBackend::Initialize(std::string& diagnostic)
{
  try
  {
    if (!m_impl->group)
      m_impl->group = std::make_shared<Detail::OpenGLDeviceGroup>();
    m_impl->group->Initialize();
    diagnostic.clear();
    return true;
  }
  catch (const std::exception& error)
  {
    diagnostic = error.what();
    m_impl->group.reset();
    return false;
  }
}

GraphicsCapabilities OpenGLBackend::Capabilities() const
{
  return {.computeShaders =
              m_impl->group && m_impl->group->display != EGL_NO_DISPLAY};
}
std::unique_ptr<IRenderDevice>
OpenGLBackend::CreateDevice(const DeviceConfig& config)
{
  if (!m_impl->group)
    m_impl->group = std::make_shared<Detail::OpenGLDeviceGroup>();
  m_impl->group->Initialize();
  return std::make_unique<OpenGLRenderDevice>(m_impl->group, config);
}
void OpenGLBackend::Shutdown() noexcept
{
  if (m_impl)
    m_impl->group.reset();
}

} // namespace Lighthouse
