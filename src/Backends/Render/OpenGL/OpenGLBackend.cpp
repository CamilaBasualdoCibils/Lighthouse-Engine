#include "OpenGLDeviceGroup.hpp"
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLRenderDevice.hpp>

#include <EGL/eglext.h>
#ifdef LIGHTHOUSE_IMGUI
#include <X11/Xlib.h>
#endif
#include <stdexcept>
#include <string>
#include <string_view>

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

EGLContext OpenGLDeviceGroup::CreateSharedContext(EGLConfig windowConfig) const
{
  const EGLint attributes[]{EGL_CONTEXT_MAJOR_VERSION,
                            4,
                            EGL_CONTEXT_MINOR_VERSION,
                            5,
                            EGL_CONTEXT_OPENGL_PROFILE_MASK,
                            EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
                            EGL_NONE};
  const auto context = eglCreateContext(
      display, windowConfig ? windowConfig : config, shareContext, attributes);
  if (context == EGL_NO_CONTEXT)
    throw std::runtime_error("Failed to create the EGL device context");
  return context;
}

std::vector<EGLConfig>
OpenGLDeviceGroup::WindowConfigs(Window& window, SwapchainFormat format) const
{
  if (nativeDisplay != window.NativeDisplayHandle())
    return {};
  if (format == SwapchainFormat::SRGB)
    return {config};
  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
  const std::string names =
      " " + std::string(extensions ? extensions : "") + " ";
  auto has = [&](const char* name)
  { return names.find(" " + std::string(name) + " ") != std::string::npos; };
  if (!has("EGL_KHR_gl_colorspace"))
    return {};
  if (format == SwapchainFormat::ScRGB)
  {
    if (!has("EGL_EXT_pixel_format_float") ||
        !has("EGL_EXT_gl_colorspace_scrgb_linear"))
      return {};
  }
  else if (format == SwapchainFormat::HDR10)
  {
    if (!has("EGL_EXT_gl_colorspace_bt2020_pq"))
      return {};
  }
  else
    return {};
  const bool floating = format == SwapchainFormat::ScRGB;
  std::vector<EGLint> attributes{
      EGL_SURFACE_TYPE,   EGL_WINDOW_BIT,     EGL_RENDERABLE_TYPE,
      EGL_OPENGL_BIT,     EGL_RED_SIZE,       floating ? 16 : 10,
      EGL_GREEN_SIZE,     floating ? 16 : 10, EGL_BLUE_SIZE,
      floating ? 16 : 10, EGL_ALPHA_SIZE,     floating ? 16 : 2};
  // Do not pass an extension-only attribute when it is not advertised.
  if (has("EGL_EXT_pixel_format_float"))
    attributes.insert(attributes.end(),
                      {EGL_COLOR_COMPONENT_TYPE_EXT,
                       floating ? EGL_COLOR_COMPONENT_TYPE_FLOAT_EXT
                                : EGL_COLOR_COMPONENT_TYPE_FIXED_EXT});
#ifdef LIGHTHOUSE_IMGUI
  XWindowAttributes nativeAttributes{};
  if (!XGetWindowAttributes(
          static_cast<Display*>(nativeDisplay),
          static_cast<::Window>(
              reinterpret_cast<std::uintptr_t>(window.NativeEGLWindowHandle())),
          &nativeAttributes))
    return {};
  attributes.insert(
      attributes.end(),
      {EGL_NATIVE_VISUAL_ID,
       static_cast<EGLint>(XVisualIDFromVisual(nativeAttributes.visual))});
#endif
  attributes.push_back(EGL_NONE);
  EGLint count{};
  if (!eglChooseConfig(display, attributes.data(), nullptr, 0, &count) ||
      !count)
    return {};
  std::vector<EGLConfig> configs(count);
  if (!eglChooseConfig(display, attributes.data(), configs.data(), count,
                       &count))
    return {};
  configs.resize(count);
  return configs;
}

EGLSurface OpenGLDeviceGroup::CreateWindowSurface(void* nativeWindow,
                                                  EGLConfig windowConfig,
                                                  SwapchainFormat format) const
{
  const EGLint attributes[]{EGL_GL_COLORSPACE_KHR,
                            format == SwapchainFormat::HDR10
                                ? EGL_GL_COLORSPACE_BT2020_PQ_EXT
                                : EGL_GL_COLORSPACE_SCRGB_LINEAR_EXT,
                            EGL_NONE};
  const auto surface = eglCreateWindowSurface(
      display, windowConfig ? windowConfig : config,
      reinterpret_cast<EGLNativeWindowType>(nativeWindow),
      format == SwapchainFormat::SRGB ? nullptr : attributes);
  if (surface == EGL_NO_SURFACE)
    throw std::runtime_error(
        "Failed to create the requested EGL window surface");
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
