#pragma once

#include <EGL/egl.h>

namespace Lighthouse::Detail {

struct OpenGLDeviceGroup {
    ~OpenGLDeviceGroup();
    void Initialize(void* nativeDisplay);
    EGLContext CreateSharedContext() const;
    EGLSurface CreateWindowSurface(void* nativeWindow) const;

    EGLDisplay display{EGL_NO_DISPLAY};
    void* nativeDisplay{};
    EGLConfig config{};
    EGLSurface pbuffer{EGL_NO_SURFACE};
    EGLContext shareContext{EGL_NO_CONTEXT};
};

} // namespace Lighthouse::Detail
