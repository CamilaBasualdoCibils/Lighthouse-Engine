#include "QueueTestHelpers.hpp"
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLRenderDevice.hpp>
#include <cassert>
#include <iostream>

using namespace Lighthouse;

template <typename F> void ExpectLogicError(F&& action) {
    bool rejected = false;
    try { action(); } catch (const std::logic_error&) { rejected = true; }
    assert(rejected);
}

int main() {
    WindowManager windows;
    windows.Initialize();
    OpenGLBackend backend;
    std::string diagnostic;
    if (!backend.Initialize(diagnostic)) throw std::runtime_error(diagnostic);
    auto device = backend.CreateDevice(
        {.queues = {
             {.capabilities = Lighthouse::RenderQueueCapability::Graphics |
                              Lighthouse::RenderQueueCapability::Compute |
                              Lighthouse::RenderQueueCapability::Transfer,
              .minCount = 1}}});
    auto& gl = dynamic_cast<OpenGLRenderDevice&>(*device);
    auto makeSwapchain = [&] {
        auto window = windows.CreateWindow({.title = "Multi-swapchain regression",
            .width = 64, .height = 64, .eglSurface = true, .visible = false});
        return device->CreateSwapchain(device->CreateSurface(*windows.GetWindow(window)), {});
    };
    auto first = makeSwapchain(), second = makeSwapchain(), untouched = makeSwapchain();
    auto source = device->CreateImage({.width = 64, .height = 64,
        .format = ImageFormat::RGBA8,
        .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSource});
    auto sourceView = device->CreateImageView({.image = source, .format = ImageFormat::RGBA8});
    auto clear = [&](ImageViewHandle view)
    {
      auto commands = device->CreateCommandList(0);
      commands->BeginRendering({.colorAttachment = view,
                                .clearColor = true,
                                .clearColorValue = {0.25f, 0.5f, 0.75f},
                                .width = 64,
                                .height = 64});
      commands->EndRendering();
      SubmitAndWait(*device, *commands);
    };
    for (int frame = 0; frame < 3; ++frame) {
        // Acquisition order differs from write and presentation order.
        auto b = device->AcquireSwapchainImage(second);
        auto a = device->AcquireSwapchainImage(first);
        auto c = device->AcquireSwapchainImage(untouched);
        assert(gl.AcquiredImageView(first) == a.view);
        assert(gl.AcquiredImageView(second) == b.view);
        ExpectLogicError([&] { device->AcquireSwapchainImage(first); });
        clear(sourceView);
        auto empty = device->CreateCommandList(0);
        SubmitAndWait(*device, *empty);
        ExpectLogicError([&] { device->GetQueue(0)->Present(untouched); });
        auto composite = device->CreateCommandList(0);
        BlitRegion region{
            .srcSubresource = {ImageAspect::Color, 0, 0, 1},
            .srcOffsets = {{{0, 0, 0}, {64, 64, 1}}},
            .dstSubresource = {ImageAspect::Color, 0, 0, 1},
            .dstOffsets = {{{0, 0, 0}, {64, 64, 1}}}};
        composite->BlitImage(source, a.image, region, ImageFilter::Nearest);
        composite->BlitImage(source, b.image, region, ImageFilter::Nearest);
        SubmitAndWait(*device, *composite);
        device->WaitIdle();
        if (frame % 2)
        {
          device->GetQueue(0)->Present(second);
          device->GetQueue(0)->Present(first);
        }
        else
        {
          device->GetQueue(0)->Present(first);
          device->GetQueue(0)->Present(second);
        }
        assert(!gl.AcquiredImageView(first));
        assert(!gl.AcquiredImageView(second));
        ExpectLogicError([&] { device->GetQueue(0)->Present(first); });
        ExpectLogicError([&] { device->GetQueue(0)->Present(untouched); });
        clear(c.view);
        device->GetQueue(0)->Present(untouched);
    }
    // A failed multi-target submission must not leave partial output presentable.
    auto a = device->AcquireSwapchainImage(first);
    auto b = device->AcquireSwapchainImage(second);
    auto failing = device->CreateCommandList(0);
    for (auto view : {a.view, b.view}) {
        failing->BeginRendering({.colorAttachment = view, .clearColor = true,
            .width = 64, .height = 64});
        failing->EndRendering();
    }
    failing->BeginRendering({.colorAttachment = {}});
    ExpectLogicError([&] { SubmitAndWait(*device, *failing); });
    ExpectLogicError([&] { device->GetQueue(0)->Present(first); });
    ExpectLogicError([&] { device->GetQueue(0)->Present(second); });
    clear(a.view);
    clear(b.view);
    device->GetQueue(0)->Present(second);
    device->GetQueue(0)->Present(first);
    // Destroying another acquired swapchain must not disturb this one.
    a = device->AcquireSwapchainImage(first);
    device->AcquireSwapchainImage(second);
    assert(device->DestroySwapchain(second));
    clear(a.view);
    device->GetQueue(0)->Present(first);
    device->DestroyImageView(sourceView);
    device->DestroyImage(source);
    device->DestroySwapchain(first);
    device->DestroySwapchain(untouched);
    std::cout << "OpenGL multi-swapchain regression tests passed\n";
}
