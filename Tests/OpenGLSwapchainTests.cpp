#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/OpenGL/OpenGLBackend.hpp>
#include <Lighthouse/OpenGL/OpenGLRenderDevice.hpp>
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
    auto device = backend.CreateDevice({});
    auto& gl = dynamic_cast<OpenGLRenderDevice&>(*device);
    auto makeSwapchain = [&] {
        auto window = windows.CreateWindow({.title = "Multi-swapchain regression",
            .width = 64, .height = 64, .eglSurface = true, .visible = false});
        return device->CreateSwapchain(*windows.GetWindow(window), {});
    };
    auto first = makeSwapchain(), second = makeSwapchain(), untouched = makeSwapchain();
    auto source = device->CreateImage({.width = 64, .height = 64,
        .format = ImageFormat::RGBA8,
        .usage = ImageUsage::ColorAttachment | ImageUsage::TransferSource});
    auto sourceView = device->CreateImageView({.image = source, .format = ImageFormat::RGBA8});
    auto clear = [&](ImageViewHandle view) {
        auto commands = device->CreateCommandList(QueueType::Graphics);
        commands->BeginRendering({.colorAttachment = view, .clearColor = true,
            .clearColorValue = {0.25f, 0.5f, 0.75f}, .width = 64, .height = 64});
        commands->EndRendering();
        device->Submit(*commands);
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
        auto empty = device->CreateCommandList(QueueType::Graphics);
        device->Submit(*empty);
        ExpectLogicError([&] { device->Present(untouched); });
        auto composite = device->CreateCommandList(QueueType::Graphics);
        BlitRegion region{
            .srcSubresource = {ImageAspect::Color, 0, 0, 1},
            .srcOffsets = {{{0, 0, 0}, {64, 64, 1}}},
            .dstSubresource = {ImageAspect::Color, 0, 0, 1},
            .dstOffsets = {{{0, 0, 0}, {64, 64, 1}}}};
        composite->BlitImage(source, a.image, region, ImageFilter::Nearest);
        composite->BlitImage(source, b.image, region, ImageFilter::Nearest);
        device->Submit(*composite);
        device->WaitIdle();
        if (frame % 2) { device->Present(second); device->Present(first); }
        else { device->Present(first); device->Present(second); }
        assert(!gl.AcquiredImageView(first));
        assert(!gl.AcquiredImageView(second));
        ExpectLogicError([&] { device->Present(first); });
        ExpectLogicError([&] { device->Present(untouched); });
        clear(c.view);
        device->Present(untouched);
    }
    // A failed multi-target submission must not leave partial output presentable.
    auto a = device->AcquireSwapchainImage(first);
    auto b = device->AcquireSwapchainImage(second);
    auto failing = device->CreateCommandList(QueueType::Graphics);
    for (auto view : {a.view, b.view}) {
        failing->BeginRendering({.colorAttachment = view, .clearColor = true,
            .width = 64, .height = 64});
        failing->EndRendering();
    }
    failing->BeginRendering({.colorAttachment = {}});
    ExpectLogicError([&] { device->Submit(*failing); });
    ExpectLogicError([&] { device->Present(first); });
    ExpectLogicError([&] { device->Present(second); });
    clear(a.view);
    clear(b.view);
    device->Present(second);
    device->Present(first);
    // Destroying another acquired swapchain must not disturb this one.
    a = device->AcquireSwapchainImage(first);
    device->AcquireSwapchainImage(second);
    assert(device->DestroySwapchain(second));
    clear(a.view);
    device->Present(first);
    device->DestroyImageView(sourceView);
    device->DestroyImage(source);
    device->DestroySwapchain(first);
    device->DestroySwapchain(untouched);
    std::cout << "OpenGL multi-swapchain regression tests passed\n";
}
