#include "VulkanDeviceState.hpp"
namespace Lighthouse
{
#ifdef LIGHTHOUSE_IMGUI
void VulkanRenderDevice::InitializeImGui(WindowManager& windows,
                                         WindowView* engineHost)
{
  auto& d = *m_impl;
  auto* context = ImGui::GetCurrentContext();
  if (d.gui.contains(context))
    return;
  Require(d.swapchainEnabled, "ImGui viewports need presentation support");
  windows.Initialize();
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
  auto* host =
      engineHost
          ? static_cast<GLFWwindow*>(engineHost->GetWindow()->NativeHandle())
          : glfwCreateWindow(1280, 720, "Lighthouse ImGui host", nullptr,
                             nullptr);
  Require(host, "Failed to create ImGui host");
  bool platform = false, renderer = false;
  try
  {
    Require(ImGui_ImplGlfw_InitForVulkan(host, true),
            "ImGui GLFW initialization failed");
    platform = true;
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = vk::ApiVersion14;
    info.Instance = static_cast<vk::Instance::CType>(d.instance);
    info.PhysicalDevice = static_cast<vk::PhysicalDevice::CType>(d.physical);
    info.Device = static_cast<vk::Device::CType>(d.device);
    info.QueueFamily = d.family;
    info.Queue = static_cast<vk::Queue::CType>(d.queue);
    info.DescriptorPoolSize = 1024;
    info.MinImageCount = 2;
    info.ImageCount =
        engineHost ? d.Find(engineHost->swapchain)->images.size() : 3;
    info.UseDynamicRendering = true;
    vk::PipelineRenderingCreateInfo rendering;
    const auto format = engineHost
                            ? d.Find(engineHost->swapchain)->format.format
                            : vk::Format::eR8G8B8A8Unorm;
    rendering.setColorAttachmentFormats(format);
    info.PipelineInfoMain.MSAASamples =
        static_cast<decltype(info.PipelineInfoMain.MSAASamples)>(
            vk::SampleCountFlagBits::e1);
    info.PipelineInfoForViewports.MSAASamples =
        info.PipelineInfoMain.MSAASamples;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = *reinterpret_cast<
        const decltype(info.PipelineInfoMain.PipelineRenderingCreateInfo)*>(
        &rendering);
    Require(ImGui_ImplVulkan_Init(&info),
            "ImGui Vulkan renderer initialization failed");
    renderer = true;
    d.gui.emplace(context, Impl::Gui{host, engineHost != nullptr});
  }
  catch (...)
  {
    if (renderer)
      ImGui_ImplVulkan_Shutdown();
    if (platform)
      ImGui_ImplGlfw_Shutdown();
    if (!engineHost)
      glfwDestroyWindow(host);
    glfwDefaultWindowHints();
    throw;
  }
  glfwDefaultWindowHints();
}
void VulkanRenderDevice::ShutdownImGui() noexcept
{
  auto& d = *m_impl;
  auto it = d.gui.find(ImGui::GetCurrentContext());
  if (it == d.gui.end())
    return;
  try
  {
    WaitIdle();
    ImGui::DestroyPlatformWindows();
    Detail::RemoveImGuiGlfwCallbacks(ImGui::GetCurrentContext());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    if (!it->second.engineHost)
      glfwDestroyWindow(it->second.host);
    d.gui.erase(it);
  }
  catch (...)
  {
  }
}
void VulkanRenderDevice::BeginImGuiFrame()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
}
void VulkanRenderDevice::RenderImGuiHost(WindowView& host)
{
  auto& d = *m_impl;
  auto* swap = d.Find(host.swapchain);
  Require(swap && swap->acquired, "ImGui host image is not acquired");
  const auto image = swap->images.at(swap->current);
  auto& state = d.images.at(image);
  std::vector<vk::Semaphore> waits;
  waits.push_back(host.WasRenderedByGraph() ? swap->finished.at(swap->current)
                                            : swap->available);
  d.Run(
      [&](vk::CommandBuffer cmd)
      {
        const bool initialized = host.WasRenderedByGraph();
        d.Barrier(cmd, image, state.layout,
                  vk::ImageLayout::eColorAttachmentOptimal);
        vk::RenderingAttachmentInfo color;
        color.setImageView(d.views.at(swap->views.at(swap->current)).native)
            .setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
            .setLoadOp(initialized ? vk::AttachmentLoadOp::eLoad
                                   : vk::AttachmentLoadOp::eClear)
            .setStoreOp(vk::AttachmentStoreOp::eStore)
            .setClearValue(vk::ClearValue(
                vk::ClearColorValue(std::array<float, 4>{0, 0, 0, 1})));
        vk::RenderingInfo rendering;
        rendering.setRenderArea(vk::Rect2D({0, 0}, swap->extent))
            .setLayerCount(1)
            .setColorAttachments(color);
        cmd.beginRendering(rendering, d.dispatch);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                        static_cast<VkCommandBuffer>(cmd));
        cmd.endRendering(d.dispatch);
        d.Barrier(cmd, image, vk::ImageLayout::eColorAttachmentOptimal,
                  vk::ImageLayout::ePresentSrcKHR);
      },
      waits, {swap->finished.at(swap->current)});
  state.layout = vk::ImageLayout::ePresentSrcKHR;
}
void VulkanRenderDevice::RenderImGuiViewports()
{
  // ImGui's GLFW adapter inherits CLIENT_API rather than setting it for
  // secondary windows. Defaults request OpenGL, whose windows cannot acquire
  // Vulkan surfaces. Reapply this before every update (other views can change
  // the process-wide GLFW hints between frames).
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  try
  {
    ImGui::UpdatePlatformWindows();
    Detail::RouteImGuiGlfwCallbacks();
    ImGui::RenderPlatformWindowsDefault();
  }
  catch (...)
  {
    glfwDefaultWindowHints();
    throw;
  }
  glfwDefaultWindowHints();
}
void VulkanRenderDevice::EndImGuiFrame() noexcept {}
#endif
} // namespace Lighthouse
