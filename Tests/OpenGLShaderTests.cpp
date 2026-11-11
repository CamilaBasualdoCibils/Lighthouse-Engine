#include "QueueTestHelpers.hpp"
#include <Lighthouse/Engine/Assets/Assets.hpp>
#include <Lighthouse/Engine/Rendering/IRenderDevice.hpp>
#include <Lighthouse/Engine/Rendering/RenderGraphComposer.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#include "OpenGLShaderTranslator.hpp"
#include "OpenGLCommandList.hpp"
#include <Lighthouse/Engine/Rendering/IRenderCommandList.hpp>
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <array>
#include <cstring>

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace {
using namespace Lighthouse;

TEST(OpenGLShaderTests, TranslatesCanonicalSpirvToGLSLWithoutAnOpenGLContext) {
    constexpr std::string_view source = R"(#version 450
layout(set = 2, binding = 5) uniform sampler2D inputTexture;
layout(location = 0) out vec4 color;
void main() { color = texture(inputTexture, vec2(0.5)); }
)";
    const auto shader = ShaderCompiler::Compile(source,
        {ShaderSourceLanguage::GLSL, ShaderStage::Fragment, "main"});
    const auto glsl = Detail::TranslateShaderToOpenGL(shader.GetSPIRV(), shader.GetStage(),
        shader.GetEntryPoint(), {16, 16, 32, 8});

    EXPECT_NE(glsl.find("#version 450"), std::string::npos);
    EXPECT_NE(glsl.find("void main()"), std::string::npos);
    EXPECT_EQ(glsl.find("set ="), std::string::npos);
    EXPECT_NE(glsl.find("binding = 0"), std::string::npos);
}

TEST(OpenGLShaderTests, RemapsEqualBindingsFromDifferentDescriptorSets) {
    constexpr std::string_view source = R"(#version 450
layout(set = 0, binding = 0) uniform FrameData { mat4 view; } frameData;
layout(set = 1, binding = 0) uniform ObjectData { mat4 model; } objectData;
void main() { gl_Position = frameData.view * objectData.model * vec4(0.0, 0.0, 0.0, 1.0); }
)";
    const auto shader = ShaderCompiler::Compile(source,
        {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
    DescriptorBindingLocations locations{
        {{0, 0}, {DescriptorType::UniformBuffer, 0, 1}},
        {{1, 0}, {DescriptorType::UniformBuffer, 1, 1}}
    };
    const auto glsl = Detail::TranslateShaderToOpenGL(shader.GetSPIRV(), shader.GetStage(),
        shader.GetEntryPoint(), {16, 16, 32, 8}, &locations);
    EXPECT_NE(glsl.find("binding = 0"), std::string::npos);
    EXPECT_NE(glsl.find("binding = 1"), std::string::npos);
}

TEST(OpenGLShaderTests, CreatesAndDestroysModulesAndLinksGraphicsPipeline) {
    WindowManager windows;
    WindowHandle windowHandle;
    try {
        windowHandle = windows.CreateWindow({
            .title = "OpenGL shader test", .width = 64, .height = 64,
            .eglSurface = true, .visible = false
        });
    } catch (const std::runtime_error& error) {
        GTEST_SKIP() << "Native OpenGL display is unavailable: " << error.what();
    }
    auto* window = windows.GetWindow(windowHandle);
    ASSERT_NE(window, nullptr);

    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic)) << diagnostic;
    auto device = backend.CreateDevice(
        {.queues = {
             {.capabilities = Lighthouse::RenderQueueCapability::Graphics |
                              Lighthouse::RenderQueueCapability::Compute |
                              Lighthouse::RenderQueueCapability::Transfer,
              .minCount = 1}}});
    const auto swapchain = device->CreateSwapchain(device->CreateSurface(*window), {});

    constexpr std::string_view vertexSource = R"(#version 450
layout(location = 0) in vec3 position;
void main() { gl_Position = vec4(position, 1.0); }
)";
    constexpr std::string_view fragmentSource = R"(#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(1.0); }
)";
    const auto vertex = ShaderCompiler::Compile(vertexSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Vertex, "main"});
    const auto fragment = ShaderCompiler::Compile(fragmentSource,
        {ShaderSourceLanguage::GLSL, ShaderStage::Fragment, "main"});
    const auto vertexModule = device->CreateShaderModule(vertex);
    const auto fragmentModule = device->CreateShaderModule(fragment);
    const auto layout = device->CreatePipelineLayout({});
    const auto pipeline =
        device->CreatePipeline({.vertexShader = vertexModule,
                                .fragmentShader = fragmentModule,
                                .colorFormats = {ImageFormat::RGBA8},
                                .layout = layout});

    device->DestroyPipeline(pipeline);
    device->DestroyPipelineLayout(layout);
    device->DestroyModule(vertexModule);
    device->DestroyModule(fragmentModule);
    EXPECT_TRUE(device->DestroySwapchain(swapchain));
    device.reset();
    backend.Shutdown();
    windows.DestroyWindow(windowHandle);
}

TEST(OpenGLSwapchainTests, RotatesRealImagesAndBlitsTheirPixels) {
    WindowManager windows;
    WindowHandle windowHandle;
    try {
        windowHandle = windows.CreateWindow({
            .title = "OpenGL swapchain test", .width = 64, .height = 64,
            .eglSurface = true, .visible = false
        });
    } catch (const std::runtime_error& error) {
        GTEST_SKIP() << "Native OpenGL display is unavailable: " << error.what();
    }
    auto* window = windows.GetWindow(windowHandle);
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic)) << diagnostic;
    auto device = backend.CreateDevice(
        {.queues = {
             {.capabilities = Lighthouse::RenderQueueCapability::Graphics |
                              Lighthouse::RenderQueueCapability::Compute |
                              Lighthouse::RenderQueueCapability::Transfer,
              .minCount = 1}}});
    EXPECT_THROW(device->CreateSwapchain(device->CreateSurface(*window), {.minImageCount = 0}), std::invalid_argument);
    const auto swapchain = device->CreateSwapchain(device->CreateSurface(*window), {.vsync = false, .minImageCount = 3});
    const auto readback = device->CreateImage({.width = 64, .height = 64,
        .format = ImageFormat::RGBA8, .usage = ImageUsage::ColorAttachment | ImageUsage::TransferDestination});
    const auto readbackView = device->CreateImageView({.image = readback, .format = ImageFormat::RGBA8});
    auto commands = device->CreateCommandList(0);
    std::array<AcquiredSwapchainImage, 3> acquired;
    for (std::uint32_t frame = 0; frame < 6; ++frame) {
        const auto image = device->AcquireSwapchainImage(swapchain);
        ASSERT_TRUE(image.image);
        ASSERT_TRUE(image.view);
        EXPECT_THROW(device->AcquireSwapchainImage(swapchain), std::logic_error);
        EXPECT_THROW(device->GetQueue(0)->Present(swapchain), std::logic_error);
        if (frame < acquired.size()) {
            acquired[frame] = image;
            for (std::uint32_t previous = 0; previous < frame; ++previous) {
                EXPECT_NE(image.image, acquired[previous].image);
                EXPECT_NE(image.view, acquired[previous].view);
            }
        } else {
            EXPECT_EQ(image.image, acquired[frame % acquired.size()].image);
            EXPECT_EQ(image.view, acquired[frame % acquired.size()].view);
        }
        commands->Clear();
        commands->BeginRendering({.colorAttachment = image.view, .clearColor = true,
            .clearColorValue = {1.f, 0.f, 0.f}, .width = 64, .height = 64});
        commands->EndRendering();
        BlitRegion region{
            .srcSubresource = {ImageAspect::Color, 0, 0, 1},
            .srcOffsets = {{{0, 0, 0}, {64, 64, 1}}},
            .dstSubresource = {ImageAspect::Color, 0, 0, 1},
            .dstOffsets = {{{0, 0, 0}, {64, 64, 1}}}
        };
        commands->BlitImage(image.image, readback, region, ImageFilter::Nearest);
        commands->BeginRendering({.colorAttachment = readbackView, .width = 64, .height = 64});
        ASSERT_NO_THROW(SubmitAndWait(*device, *commands));
        std::array<unsigned char, 4> pixel{};
        const auto pixels = device->ReadImage(readback);
        std::memcpy(pixel.data(), pixels.data() + (32 * 64 + 32) * 4,
                    pixel.size());
        EXPECT_EQ(pixel, (std::array<unsigned char, 4>{255, 0, 0, 255}));
        commands->Clear();
        commands->EndRendering();
        // Also verify the acquired image can be used as a blit destination.
        commands->BlitImage(readback, image.image, region, ImageFilter::Nearest);
        ASSERT_NO_THROW(SubmitAndWait(*device, *commands));
        // Presentation state belongs to the queue's context, not this thread.
        ASSERT_NO_THROW(device->GetQueue(0)->Present(swapchain));
    }
    EXPECT_TRUE(device->DestroySwapchain(swapchain));
    EXPECT_FALSE(device->IsValid(swapchain));
    EXPECT_THROW(device->AcquireSwapchainImage(swapchain), std::invalid_argument);
    EXPECT_THROW(device->CreateImageView({.image = acquired[0].image, .format = ImageFormat::RGBA8}), std::out_of_range);
    device->DestroyImageView(readbackView);
    device->DestroyImage(readback);
    device.reset();
    backend.Shutdown();
    windows.DestroyWindow(windowHandle);
}

TEST(OpenGLWindowViewTests, RendererCreatesAndOwnsWindowWhileStandaloneWindowsRemainIndependent) {
    WindowManager windows;
    WindowHandle standalone;
    try {
        standalone = windows.CreateWindow({.title = "Standalone", .width = 64, .height = 64,
            .eglSurface = true, .visible = false});
    } catch (const std::runtime_error& error) {
        GTEST_SKIP() << "Native OpenGL display is unavailable: " << error.what();
    }
    OpenGLBackend backend;
    std::string diagnostic;
    ASSERT_TRUE(backend.Initialize(diagnostic)) << diagnostic;
    WindowHandle ownedWindow;
    {
      RenderGraphComposer renderer(
          backend.CreateDevice(
              {.queues = {{.capabilities =
                               Lighthouse::RenderQueueCapability::Graphics |
                               Lighthouse::RenderQueueCapability::Compute |
                               Lighthouse::RenderQueueCapability::Transfer,
                           .minCount = 1}}}),
          &windows);
      const auto viewHandle = renderer.CreateView<WindowView>(
          RenderWindowDesc{.title = "RenderGraphComposer window",
                           .width = 96,
                           .height = 80,
                           .resizable = false,
                           .visible = false,
                           .decorated = false});
      auto* view = renderer.GetView<WindowView>(viewHandle);
      ASSERT_NE(view, nullptr);
      auto* window = view->GetWindow();
      ASSERT_NE(window, nullptr);
      ownedWindow = window->Handle();
      EXPECT_EQ(window->Title(), "RenderGraphComposer window");
      EXPECT_EQ(window->Width(), 96u);
      EXPECT_EQ(window->Height(), 80u);
      EXPECT_FALSE(window->IsVisible());
      EXPECT_NE(window->NativeEGLWindowHandle(), nullptr);
      EXPECT_NE(window->NativeDisplayHandle(), nullptr);
      const auto size = window->GetFramebufferSize();
      EXPECT_EQ(size.width, window->FramebufferWidth());
      EXPECT_EQ(size.height, window->FramebufferHeight());
      EXPECT_GT(size.width, 0u);
      EXPECT_GT(size.height, 0u);
      RenderGraph graph;
      ViewBuildContext context{graph, *renderer.GetRenderDevice()};
      view->BuildGraph(context);
      const auto swapchain = view->swapchain;
      EXPECT_TRUE(renderer.GetRenderDevice()->IsValid(swapchain));
      EXPECT_TRUE(renderer.DestroyView(viewHandle));
      EXPECT_EQ(renderer.GetView(viewHandle), nullptr);
      EXPECT_EQ(windows.GetWindow(ownedWindow), nullptr);
      EXPECT_FALSE(renderer.GetRenderDevice()->IsValid(swapchain));
      EXPECT_NE(windows.GetWindow(standalone), nullptr);
      EXPECT_THROW(
          renderer.CreateView<WindowView>(RenderWindowDesc{.width = 0}),
          std::invalid_argument);
      const auto remaining = renderer.CreateWindowView(
          RenderWindowDesc{.title = "Destructor cleanup",
                           .width = 64,
                           .height = 64,
                           .visible = false});
      auto* remainingView = renderer.GetView<WindowView>(remaining);
      ownedWindow = remainingView->GetWindow()->Handle();
      remainingView->BuildGraph(context);
    }
    EXPECT_EQ(windows.GetWindow(ownedWindow), nullptr);
    EXPECT_NE(windows.GetWindow(standalone), nullptr);
    windows.DestroyWindow(standalone);
    backend.Shutdown();
}

} // namespace

TEST(OpenGLMemoryBarrierTests, MapsDestinationConsumersWithoutBroadeningToAllBits)
{
    using namespace Lighthouse;
    MemoryBarrierDesc desc{.srcStageMask = PipelineStage::ComputeShader,
        .srcAccessMask = PipelineAccess::ShaderStorageWrite,
        .dstStageMask = PipelineStage::AllCommands};
    const std::pair<PipelineAccess, std::uint32_t> cases[] = {
        {PipelineAccess::IndirectCommandRead, GL_COMMAND_BARRIER_BIT},
        {PipelineAccess::IndexRead, GL_ELEMENT_ARRAY_BARRIER_BIT},
        {PipelineAccess::VertexAttributeRead, GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT},
        {PipelineAccess::UniformRead, GL_UNIFORM_BARRIER_BIT},
        {PipelineAccess::ShaderSampledRead, GL_TEXTURE_FETCH_BARRIER_BIT},
        {PipelineAccess::ShaderStorageRead, GL_SHADER_STORAGE_BARRIER_BIT |
            GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT},
        {PipelineAccess::ColorAttachmentWrite, GL_FRAMEBUFFER_BARRIER_BIT},
        {PipelineAccess::DepthStencilAttachmentRead, GL_FRAMEBUFFER_BARRIER_BIT},
        {PipelineAccess::TransferRead, GL_BUFFER_UPDATE_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT |
            GL_PIXEL_BUFFER_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT},
        {PipelineAccess::HostRead, GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT}
    };
    for (auto [access, expected] : cases) {
        desc.dstAccessMask = access;
        EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), expected) << static_cast<std::uint64_t>(access);
    }
    desc.dstAccessMask = {PipelineAccess::IndexRead, PipelineAccess::VertexAttributeRead,
        PipelineAccess::IndirectCommandRead};
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), GL_ELEMENT_ARRAY_BARRIER_BIT |
        GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_COMMAND_BARRIER_BIT);
    // OpenGL cannot narrow the source stage/access scope. A broad source must
    // not turn a precise destination into GL_ALL_BARRIER_BITS.
    desc.srcStageMask = PipelineStage::AllCommands;
    desc.srcAccessMask = PipelineAccess::MemoryWrite;
    desc.dstAccessMask = PipelineAccess::ShaderSampledRead;
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), GL_TEXTURE_FETCH_BARRIER_BIT);
}
TEST(OpenGLMemoryBarrierTests, HandlesEmptyExecutionAndHostScopes)
{
    using namespace Lighthouse;
    MemoryBarrierDesc desc{};
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), 0u);
    desc.srcStageMask = PipelineStage::ComputeShader;
    desc.dstStageMask = PipelineStage::FragmentShader;
    // Execution-only dependencies require conservative ordering in OpenGL.
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), GL_ALL_BARRIER_BITS);
    desc.dstAccessMask = PipelineAccess::MemoryRead;
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), GL_ALL_BARRIER_BITS);
    desc.dstAccessMask = PipelineAccess::UniformRead;
    desc.srcStageMask = PipelineStage::Host;
    desc.srcAccessMask = PipelineAccess::HostWrite;
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), GL_UNIFORM_BARRIER_BIT |
        GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    desc.dstStageMask = PipelineStage::None;
    EXPECT_EQ(Detail::OpenGLMemoryBarrierBits(desc), 0u);
}
TEST(OpenGLMemoryBarrierTests, RecordsAnOwnedDescriptorAndRequiresRecording)
{
    using namespace Lighthouse;
    OpenGLCommandList commands(1);
    MemoryBarrierDesc desc{.srcStageMask = PipelineStage::ComputeShader,
        .srcAccessMask = PipelineAccess::ShaderStorageWrite,
        .dstStageMask = PipelineStage::FragmentShader,
        .dstAccessMask = PipelineAccess::ShaderStorageRead};
    EXPECT_THROW(commands.MemoryBarrier(desc), std::logic_error);
    commands.Begin();
    commands.MemoryBarrier(desc);
    desc.dstAccessMask = PipelineAccess::MemoryRead;
    commands.End();
    ASSERT_EQ(commands.Commands().size(), 1u);
    const auto& recorded = std::get<OpenGLCommandList::BarrierCommand>(commands.Commands()[0]).desc;
    EXPECT_EQ(*recorded.srcStageMask, static_cast<std::uint64_t>(PipelineStage::ComputeShader));
    EXPECT_EQ(*recorded.srcAccessMask, static_cast<std::uint64_t>(PipelineAccess::ShaderStorageWrite));
    EXPECT_EQ(*recorded.dstStageMask, static_cast<std::uint64_t>(PipelineStage::FragmentShader));
    EXPECT_EQ(*recorded.dstAccessMask, static_cast<std::uint64_t>(PipelineAccess::ShaderStorageRead));
}
