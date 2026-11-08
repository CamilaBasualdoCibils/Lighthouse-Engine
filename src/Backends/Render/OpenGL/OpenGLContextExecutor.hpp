#pragma once
#include "OpenGLDeviceGroup.hpp"
#define GL_GLEXT_PROTOTYPES
#include <EGL/eglext.h>
#include <GL/glcorearb.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>

namespace Lighthouse::Detail
{
// One owner thread and one native context. No device execution lock is used.
class OpenGLContextExecutor
{
  std::shared_ptr<OpenGLDeviceGroup> group;
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<std::function<void()>> jobs;
  bool accepting = true;
  std::thread thread;
  std::exception_ptr failure;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface pbuffer = EGL_NO_SURFACE;

public:
  OpenGLContextExecutor(std::shared_ptr<OpenGLDeviceGroup> g,
                        std::function<void()> enter,
                        std::function<void()> leave)
      : group(std::move(g))
  {
    std::promise<void> started;
    auto ready = started.get_future();
    thread = std::thread(
        [this, enter = std::move(enter), leave = std::move(leave),
         started = std::move(started)]() mutable
        {
          bool initialized = false;
          try
          {
            if (!eglBindAPI(EGL_OPENGL_API))
              throw std::runtime_error("Worker could not bind OpenGL API");
            const char* extensions =
                eglQueryString(group->display, EGL_EXTENSIONS);
            const bool configless =
                extensions && std::string_view(extensions)
                                      .find("EGL_KHR_no_config_context") !=
                                  std::string_view::npos;
            context = group->CreateSharedContext(nullptr, configless);
            const EGLint attributes[]{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
            pbuffer = eglCreatePbufferSurface(group->display, group->config,
                                              attributes);
            if (pbuffer == EGL_NO_SURFACE)
              throw std::runtime_error("Worker pbuffer creation failed");
            MakeCurrent();
            enter();
            initialized = true;
            started.set_value();
            for (;;)
            {
              std::function<void()> job;
              {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return !jobs.empty() || !accepting; });
                if (jobs.empty())
                  break;
                job = std::move(jobs.front());
                jobs.pop_front();
              }
              try
              {
                MakeCurrent();
              }
              catch (...)
              {
                std::lock_guard lock(mutex);
                if (!failure)
                  failure = std::current_exception();
              }
              try
              {
                job();
                MakeCurrent();
              }
              catch (...)
              {
                std::lock_guard lock(mutex);
                if (!failure)
                  failure = std::current_exception();
              }
              job = {}; // Release captures while this thread still owns its
                        // context.
            }
          }
          catch (...)
          {
            if (!initialized)
              started.set_exception(std::current_exception());
            else
            {
              std::lock_guard lock(mutex);
              failure = std::current_exception();
            }
          }
          if (initialized)
          {
            try
            {
              MakeCurrent();
              leave();
            }
            catch (...)
            {
            }
          }
          eglMakeCurrent(group->display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT);
          if (pbuffer != EGL_NO_SURFACE)
            eglDestroySurface(group->display, pbuffer);
          if (context != EGL_NO_CONTEXT)
            eglDestroyContext(group->display, context);
        });
    try
    {
      ready.get();
    }
    catch (...)
    {
      if (thread.joinable())
        thread.join();
      throw;
    }
  }
  ~OpenGLContextExecutor()
  {
    Stop();
  }
  OpenGLContextExecutor(const OpenGLContextExecutor&) = delete;
  void MakeCurrent()
  {
    if (eglGetCurrentContext() == context &&
        eglGetCurrentSurface(EGL_READ) == pbuffer &&
        eglGetCurrentSurface(EGL_DRAW) == pbuffer)
      return;
    if (!eglMakeCurrent(group->display, pbuffer, pbuffer, context))
      throw std::runtime_error("Cannot activate OpenGL queue context");
  }
  EGLContext Native() const noexcept
  {
    return context;
  }
  void StopAccepting()
  {
    {
      std::lock_guard lock(mutex);
      accepting = false;
    }
    wake.notify_all();
  }
  void Stop()
  {
    StopAccepting();
    if (thread.joinable())
      thread.join();
  }
  void Post(std::function<void()> job)
  {
    {
      std::lock_guard lock(mutex);
      if (!accepting)
        throw std::logic_error("OpenGL queue is shutting down");
      jobs.push_back(std::move(job));
    }
    wake.notify_one();
  }
  template <class F> auto Invoke(F&& fn) -> std::invoke_result_t<F>
  {
    if (std::this_thread::get_id() == thread.get_id())
      return std::forward<F>(fn)();
    using Result = std::invoke_result_t<F>;
    auto restoreSurface = [this, fn = std::forward<F>(fn)]() mutable -> Result
    {
      try
      {
        if constexpr (std::is_void_v<Result>)
        {
          fn();
          MakeCurrent();
        }
        else
        {
          Result value = fn();
          MakeCurrent();
          return value;
        }
      }
      catch (...)
      {
        // Release a window surface before the caller observes completion or
        // failure and is allowed to resize/destroy it.
        try
        {
          MakeCurrent();
        }
        catch (...)
        {
        }
        throw;
      }
    };
    auto task = std::make_shared<std::packaged_task<Result()>>(
        std::move(restoreSurface));
    auto result = task->get_future();
    Post([task] { (*task)(); });
    return result.get();
  }
  void RethrowFailure()
  {
    std::exception_ptr error;
    {
      std::lock_guard lock(mutex);
      error = std::exchange(failure, {});
    }
    if (error)
      std::rethrow_exception(error);
  }
};
} // namespace Lighthouse::Detail
