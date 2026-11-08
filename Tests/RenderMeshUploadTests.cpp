#include "QueueTestHelpers.hpp"
#include <Lighthouse/Engine/Assets/Mesh.hpp>
#include <Lighthouse/Engine/Assets/ShaderCompiler.hpp>
#include <Lighthouse/Engine/Rendering/Renderer.hpp>
#include <Lighthouse/Backends/Render/OpenGL/OpenGLBackend.hpp>
#define GL_GLEXT_PROTOTYPES
#include <GL/glcorearb.h>
#include <array>
#include <cstring>
#include <gtest/gtest.h>

using namespace Lighthouse;

TEST(RenderMeshUploadTests,
     SharedRangesSurviveGrowthAndRenderWithNonzeroOffsets)
{
  WindowManager windows;
  OpenGLBackend backend;
  std::string diagnostic;
  ASSERT_TRUE(backend.Initialize(diagnostic));
  Renderer renderer(
      backend.CreateDevice(
          {.queues = {{.capabilities =
                           Lighthouse::RenderQueueCapability::Graphics |
                           Lighthouse::RenderQueueCapability::Compute |
                           Lighthouse::RenderQueueCapability::Transfer,
                       .minCount = 1}}}),
      &windows);
  renderer.CreateWindowView({.title = "Mesh upload regression",
                             .width = 64,
                             .height = 64,
                             .visible = false});
  auto& device = *renderer.GetRenderDevice();
  Mesh3D left, right;
  left.vertices = {
      {{-0.9f, -0.8f, 0.5f}}, {{-0.1f, -0.8f, 0.5f}}, {{-0.5f, 0.8f, 0.5f}}};
  right.vertices = {
      {{0.1f, -0.8f, 0.5f}}, {{0.9f, -0.8f, 0.5f}}, {{0.5f, 0.8f, 0.5f}}};
  left.indices = right.indices = {0, 1, 2};
  auto leftData = left.ToRenderMesh();
  const std::array<std::uint16_t, 3> shortIndices{0, 1, 2};
  const auto shortBytes = std::as_bytes(std::span(shortIndices));
  leftData.indexData.assign(shortBytes.begin(), shortBytes.end());
  leftData.indexBuffer->format = IndexFormat::UInt16;
  const auto first = renderer.UploadMesh(leftData);
  const auto oldBuffer =
      renderer.GetRenderMesh(first)->vertexBuffers.front().buffer;
  const auto second = renderer.UploadMesh(right);
  const auto* a = renderer.GetRenderMesh(first);
  const auto* b = renderer.GetRenderMesh(second);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_FALSE(device.IsValid(oldBuffer));
  EXPECT_EQ(a->vertexBuffers.front().buffer, b->vertexBuffers.front().buffer);
  EXPECT_EQ(a->indexBuffer->buffer, b->indexBuffer->buffer);
  EXPECT_EQ(a->vertexBuffers.front().offset, 0u);
  EXPECT_EQ(b->vertexBuffers.front().offset, 3 * sizeof(Mesh3DVertex));
  EXPECT_EQ(b->indexBuffer->offset, 3 * sizeof(std::uint32_t));
  EXPECT_EQ(a->indexBuffer->format, IndexFormat::UInt32);
  EXPECT_EQ(renderer.GetRenderMeshUploadStatus(first),
            RenderMeshUploadStatus::Ready);
  EXPECT_TRUE(a->vertexData.empty());
  EXPECT_TRUE(a->indexData.empty());
  auto invalid = right;
  invalid.indices.back() = 3;
  EXPECT_THROW(renderer.UploadMesh(invalid), std::invalid_argument);
  EXPECT_TRUE(renderer.IsValid(first));
  EXPECT_TRUE(renderer.IsValid(second));

  const auto vs = device.CreateShaderModule(
      ShaderCompiler::Compile(R"(#version 450
layout(location=0) in vec3 position;
void main() { gl_Position=vec4(position,1); }
)",
                              {.stage = ShaderStage::Vertex}));
  const auto fs = device.CreateShaderModule(
      ShaderCompiler::Compile(R"(#version 450
layout(location=0) out vec4 color;
void main() { color=vec4(1); }
)",
                              {.stage = ShaderStage::Fragment}));
  const auto layout = device.CreatePipelineLayout({});
  const auto pipeline =
      device.CreatePipeline({.vertexShader = vs,
                             .fragmentShader = fs,
                             .vertexLayout = a->layout,
                             .colorFormats = {ImageFormat::RGBA8},
                             .layout = layout});
  const auto image =
      device.CreateImage({.width = 64,
                          .height = 64,
                          .format = ImageFormat::RGBA8,
                          .usage = {ImageUsage::ColorAttachment}});
  const auto imageView =
      device.CreateImageView({.image = image, .format = ImageFormat::RGBA8});
  auto commands = device.CreateCommandList(0);
  commands->Begin();
  commands->BeginRendering({.colorAttachment = imageView,
                            .clearColor = true,
                            .width = 64,
                            .height = 64});
  commands->BindPipeline(pipeline);
  commands->BindVertexBuffer(a->vertexBuffers.front().buffer,
                             sizeof(Mesh3DVertex));
  commands->BindIndexBuffer(a->indexBuffer->buffer);
  for (const auto* mesh : {a, b})
  {
    commands->DrawIndexedRange(
        mesh->indexCount,
        static_cast<std::uint32_t>(mesh->indexBuffer->offset / 4),
        static_cast<std::int32_t>(mesh->vertexBuffers.front().offset /
                                  sizeof(Mesh3DVertex)));
  }
  commands->EndRendering();
  commands->BindTexture(0, image);
  commands->End();
  SubmitAndWait(device, *commands);
  // Queue contexts stay on their owner threads; diagnostics use the device API.
  const auto readbackBytes = device.ReadImage(image);
  std::array<unsigned char, 64 * 64 * 4> pixels{};
  std::memcpy(pixels.data(), readbackBytes.data(), pixels.size());
  EXPECT_GT(pixels[(24 * 64 + 16) * 4], 240);
  EXPECT_GT(pixels[(24 * 64 + 48) * 4], 240);
  EXPECT_LT(pixels[(60 * 64 + 32) * 4], 10);
  device.DestroyImageView(imageView);
  device.DestroyImage(image);
  device.DestroyPipeline(pipeline);
  device.DestroyPipelineLayout(layout);
  device.DestroyModule(vs);
  device.DestroyModule(fs);
  EXPECT_TRUE(renderer.DestroyRenderMesh(first));
  EXPECT_FALSE(renderer.IsValid(first));
  EXPECT_TRUE(renderer.IsValid(second));
  const auto replacement = renderer.UploadMesh(left);
  EXPECT_EQ(first.index, replacement.index);
  EXPECT_NE(first.generation, replacement.generation);
  EXPECT_TRUE(renderer.IsValid(second));
  EXPECT_TRUE(renderer.IsValid(replacement));
}

