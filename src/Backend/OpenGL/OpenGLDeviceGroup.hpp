#pragma once

#include <EGL/egl.h>

namespace Lighthouse::Detail {

struct OpenGLDeviceGroup {
    ~OpenGLDeviceGroup();
    void Initialize(void* nativeDisplay);
    EGLContext CreateSharedContext() const;
    EGLSurface CreateWindowSurface(void* nativeWindow) const;

    EGLDisplay display{EGL_NO_DISPLAY};
    EGLConfig config{};
    EGLContext shareContext{EGL_NO_CONTEXT};
};

} // namespace Lighthouse::Detail
