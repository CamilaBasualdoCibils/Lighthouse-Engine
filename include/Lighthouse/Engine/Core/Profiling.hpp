#pragma once

// Keep Tracy headers and instrumentation out of non-profiling builds.
#if defined(LIGHTHOUSE_ENABLE_TRACY) && LIGHTHOUSE_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#define LIGHTHOUSE_PROFILE_SCOPE(name) ZoneScopedN(name)
#define LIGHTHOUSE_PROFILE_FRAME() FrameMark
#else
#define LIGHTHOUSE_PROFILE_SCOPE(name) ((void)0)
#define LIGHTHOUSE_PROFILE_FRAME() ((void)0)
#endif
