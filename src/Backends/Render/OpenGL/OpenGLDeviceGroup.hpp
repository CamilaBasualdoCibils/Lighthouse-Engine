#pragma once

#include <EGL/egl.h>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <memory>

namespace Lighthouse::Detail
{

struct OpenGLDeviceGroup
{
  ~OpenGLDeviceGroup();
  void Initialize(void* nativeDisplay = nullptr);
  EGLContext CreateSharedContext() const;
  EGLSurface CreateWindowSurface(void* nativeWindow) const;

  std::unique_ptr<WindowManager> platform;
  EGLDisplay display{EGL_NO_DISPLAY};
  void* nativeDisplay{};
  EGLConfig config{};
  EGLSurface pbuffer{EGL_NO_SURFACE};
  EGLContext shareContext{EGL_NO_CONTEXT};
};

} // namespace Lighthouse::Detail
