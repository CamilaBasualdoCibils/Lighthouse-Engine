#include <Lighthouse/Vulkan/VulkanRenderDevice.hpp>
#include <Lighthouse/Vulkan/VulkanCommandList.hpp>
#include <Lighthouse/Engine/Window/WindowManager.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace Lighthouse {
namespace {

template<class H>
struct HandleHash { std::size_t operator()(H handle) const noexcept { return handle.index; } };

void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
}

VkFormat ToVkFormat(ImageFormat format) {
    switch (format) {
    case ImageFormat::R8: return VK_FORMAT_R8_UNORM;
    case ImageFormat::RG8: return VK_FORMAT_R8G8_UNORM;
    case ImageFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case ImageFormat::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case ImageFormat::Depth16: return VK_FORMAT_D16_UNORM;
    case ImageFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
    case ImageFormat::Depth32: return VK_FORMAT_D32_SFLOAT;
    case ImageFormat::Undefined: break;
    }
    throw std::invalid_argument("Unsupported Vulkan image format");
}

VkFormat ToVkVertexFormat(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UInt: return VK_FORMAT_R32_UINT;
    case VertexFormat::UInt2: return VK_FORMAT_R32G32_UINT;
    case VertexFormat::UInt3: return VK_FORMAT_R32G32B32_UINT;
    case VertexFormat::UInt4: return VK_FORMAT_R32G32B32A32_UINT;
    }
    throw std::invalid_argument("Unsupported Vulkan vertex format");
}

VkPrimitiveTopology ToVkTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::Triangles: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::Lines: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::Points: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    throw std::invalid_argument("Unsupported Vulkan primitive topology");
}

} // namespace

struct VulkanRenderDevice::Impl {
    struct QueueFamily { std::uint32_t index{}; };
    struct BufferResource { VkBuffer buffer{}; VkDeviceMemory memory{}; };
    struct PipelineResource { VkPipeline pipeline{}; VkPipelineLayout layout{}; };

    explicit Impl(const DeviceConfig& config) {
        CreateInstance(config);
    }

    void CreateInstance(const DeviceConfig& config) {
        std::uint32_t availableCount{};
        Check(vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr), "vkEnumerateInstanceExtensionProperties");
        std::vector<VkExtensionProperties> available(availableCount);
        Check(vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data()), "vkEnumerateInstanceExtensionProperties");
        constexpr const char* surfaceExtensions[]{
            "VK_KHR_surface",
            "VK_KHR_wayland_surface",
            "VK_KHR_xlib_surface",
            "VK_KHR_xcb_surface",
            "VK_KHR_win32_surface",
            "VK_EXT_metal_surface",
            "VK_KHR_android_surface"
        };
        std::vector<const char*> enabledExtensions;
        for (const auto* extension : surfaceExtensions) {
            const auto found = std::find_if(available.begin(), available.end(), [extension](const auto& item) {
                return std::strcmp(item.extensionName, extension) == 0;
            });
            if (found != available.end()) enabledExtensions.push_back(extension);
        }
        if (std::find_if(enabledExtensions.begin(), enabledExtensions.end(), [](const char* name) {
                return std::strcmp(name, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
            }) == enabledExtensions.end())
            throw std::runtime_error("Vulkan instance does not expose VK_KHR_surface");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        const std::string applicationName(config.applicationName);
        app.pApplicationName = applicationName.c_str();
        app.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        app.pEngineName = "Lighthouse";
        app.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
        app.apiVersion = VK_API_VERSION_1_4;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        create.pApplicationInfo = &app;
        create.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
        create.ppEnabledExtensionNames = enabledExtensions.data();
        Check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
    }

    void SelectPhysicalDevice() {
        std::uint32_t count{};
        Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        if (!count) throw std::runtime_error("No Vulkan physical devices are available");
        std::vector<VkPhysicalDevice> devices(count);
        Check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        for (auto candidate : devices) {
            VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            features.pNext = &features13;
            vkGetPhysicalDeviceFeatures2(candidate, &features);
            if (!features13.dynamicRendering || !features13.synchronization2) continue;
            std::uint32_t familyCount{};
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
            for (std::uint32_t i = 0; i < familyCount; ++i) {
                VkBool32 present{};
                Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "vkGetPhysicalDeviceSurfaceSupportKHR");
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physicalDevice = candidate;
                    queueFamily = i;
                    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
                    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
                    return;
                }
            }
        }
        throw std::runtime_error("No Vulkan 1.4 device with dynamic rendering and graphics/present support was found");
    }

    void CreateLogicalDevice() {
        constexpr float priority = 1.0f;
        VkDeviceQueueCreateInfo queueCreate{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreate.queueFamilyIndex = queueFamily;
        queueCreate.queueCount = 1;
        queueCreate.pQueuePriorities = &priority;
        VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        features13.dynamicRendering = VK_TRUE;
        features13.synchronization2 = VK_TRUE;
        const char* extensions[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo create{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        create.pNext = &features13;
        create.queueCreateInfoCount = 1;
        create.pQueueCreateInfos = &queueCreate;
        create.enabledExtensionCount = 1;
        create.ppEnabledExtensionNames = extensions;
        Check(vkCreateDevice(physicalDevice, &create, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
    }

    void CreateSwapchainResources(const SwapchainConfig& config) {
        VkSurfaceCapabilitiesKHR caps{};
        Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
        std::uint32_t formatCount{};
        Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (!formatCount) throw std::runtime_error("Vulkan surface has no supported formats");
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()), "vkGetPhysicalDeviceSurfaceFormatsKHR");
        surfaceFormat = formats.front();
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surfaceFormat = format;
                break;
            }
        }
        if (caps.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) extent = caps.currentExtent;
        else {
            int width{}, height{};
            glfwGetFramebufferSize(window, &width, &height);
            extent.width = std::clamp(static_cast<std::uint32_t>(std::max(width, 1)), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(static_cast<std::uint32_t>(std::max(height, 1)), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        std::uint32_t imageCount = std::max(caps.minImageCount + 1, config.imageCount);
        if (caps.maxImageCount && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;
        VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        create.surface = surface;
        create.minImageCount = imageCount;
        create.imageFormat = surfaceFormat.format;
        create.imageColorSpace = surfaceFormat.colorSpace;
        create.imageExtent = extent;
        create.imageArrayLayers = 1;
        create.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.preTransform = caps.currentTransform;
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
            create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            create.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            create.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else
            create.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        create.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!config.vsync) {
            std::uint32_t modeCount{};
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr), "vkGetPhysicalDeviceSurfacePresentModesKHR");
            std::vector<VkPresentModeKHR> modes(modeCount);
            Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data()), "vkGetPhysicalDeviceSurfacePresentModesKHR");
            if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
                create.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end())
                create.presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
        create.clipped = VK_TRUE;
        Check(vkCreateSwapchainKHR(device, &create, nullptr, &swapchain), "vkCreateSwapchainKHR");
        Check(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr), "vkGetSwapchainImagesKHR");
        swapchainImages.resize(imageCount);
        Check(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data()), "vkGetSwapchainImagesKHR");
        swapchainViews.resize(imageCount);
        imageInitialized.resize(imageCount, false);
        for (std::size_t i = 0; i < swapchainImages.size(); ++i) {
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            view.image = swapchainImages[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = surfaceFormat.format;
            view.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            Check(vkCreateImageView(device, &view, nullptr, &swapchainViews[i]), "vkCreateImageView");
        }
    }

    void CreateCommandResources() {
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queueFamily;
        Check(vkCreateCommandPool(device, &pool, nullptr, &commandPool), "vkCreateCommandPool");
        VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocate.commandPool = commandPool;
        allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate.commandBufferCount = 1;
        Check(vkAllocateCommandBuffers(device, &allocate, &commandBuffer), "vkAllocateCommandBuffers");
        VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        Check(vkCreateSemaphore(device, &semaphore, nullptr, &imageAvailable), "vkCreateSemaphore");
        Check(vkCreateSemaphore(device, &semaphore, nullptr, &renderFinished), "vkCreateSemaphore");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        Check(vkCreateFence(device, &fence, nullptr, &inFlight), "vkCreateFence");
    }

    std::uint32_t FindMemoryType(std::uint32_t bits, VkMemoryPropertyFlags desired) const {
        for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & desired) == desired) return i;
        throw std::runtime_error("No Vulkan memory type satisfies the buffer allocation");
    }

    GLFWwindow* window{};
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice physicalDevice{};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    std::uint32_t queueFamily{};
    VkDevice device{};
    VkQueue queue{};
    VkSwapchainKHR swapchain{};
    VkSurfaceFormatKHR surfaceFormat{};
    VkExtent2D extent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<bool> imageInitialized;
    VkCommandPool commandPool{};
    VkCommandBuffer commandBuffer{};
    VkSemaphore imageAvailable{}, renderFinished{};
    VkFence inFlight{};
    std::uint32_t nextBuffer{1}, nextShader{1}, nextPipeline{1};
    std::unordered_map<BufferHandle, BufferResource, HandleHash<BufferHandle>> buffers;
    std::unordered_map<ShaderModuleHandle, VkShaderModule, HandleHash<ShaderModuleHandle>> shaders;
    std::unordered_map<GraphicsPipelineHandle, PipelineResource, HandleHash<GraphicsPipelineHandle>> pipelines;
};

VulkanRenderDevice::VulkanRenderDevice(const DeviceConfig& config) : m_impl(std::make_unique<Impl>(config)) {}
VulkanRenderDevice::~VulkanRenderDevice() {
    if (!m_impl) return;
    auto& d = *m_impl;
    if (d.device) vkDeviceWaitIdle(d.device);
    for (const auto& [_, pipeline] : d.pipelines) { vkDestroyPipeline(d.device, pipeline.pipeline, nullptr); vkDestroyPipelineLayout(d.device, pipeline.layout, nullptr); }
    for (const auto& [_, shader] : d.shaders) vkDestroyShaderModule(d.device, shader, nullptr);
    for (const auto& [_, buffer] : d.buffers) { vkDestroyBuffer(d.device, buffer.buffer, nullptr); vkFreeMemory(d.device, buffer.memory, nullptr); }
    for (auto view : d.swapchainViews) vkDestroyImageView(d.device, view, nullptr);
    if (d.inFlight) vkDestroyFence(d.device, d.inFlight, nullptr);
    if (d.renderFinished) vkDestroySemaphore(d.device, d.renderFinished, nullptr);
    if (d.imageAvailable) vkDestroySemaphore(d.device, d.imageAvailable, nullptr);
    if (d.commandPool) vkDestroyCommandPool(d.device, d.commandPool, nullptr);
    if (d.swapchain) vkDestroySwapchainKHR(d.device, d.swapchain, nullptr);
    if (d.device) vkDestroyDevice(d.device, nullptr);
    if (d.surface) vkDestroySurfaceKHR(d.instance, d.surface, nullptr);
    if (d.instance) vkDestroyInstance(d.instance, nullptr);
}

void VulkanRenderDevice::CreateSwapchain(Window& window, const SwapchainConfig& config) {
    auto& d = *m_impl;
    if (d.swapchain || d.surface) throw std::logic_error("Vulkan device already has a swapchain");
    d.window = static_cast<GLFWwindow*>(window.NativeHandle());
    if (!d.window) throw std::invalid_argument("Vulkan swapchain requires a native GLFW window");
    Check(glfwCreateWindowSurface(d.instance, d.window, nullptr, &d.surface), "glfwCreateWindowSurface");
    try {
        d.SelectPhysicalDevice();
        d.CreateLogicalDevice();
        d.CreateSwapchainResources(config);
        d.CreateCommandResources();
    } catch (...) {
        if (d.device) { vkDestroyDevice(d.device, nullptr); d.device = VK_NULL_HANDLE; }
        if (d.surface) { vkDestroySurfaceKHR(d.instance, d.surface, nullptr); d.surface = VK_NULL_HANDLE; }
        d.window = nullptr;
        throw;
    }
}

ImageHandle VulkanRenderDevice::CreateImage(const ImageDesc&) { throw std::runtime_error("Vulkan images are not part of the triangle baseline"); }
ImageViewHandle VulkanRenderDevice::CreateImageView(const ImageViewDesc&) { throw std::runtime_error("Vulkan image views are not part of the triangle baseline"); }

BufferHandle VulkanRenderDevice::CreateBuffer(const BufferDesc& desc) {
    auto& d = *m_impl;
    VkBufferUsageFlags usage{};
    const auto bits = static_cast<std::uint32_t>(desc.usage);
    if (bits & static_cast<std::uint32_t>(BufferUsage::Vertex)) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (bits & static_cast<std::uint32_t>(BufferUsage::Index)) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (bits & static_cast<std::uint32_t>(BufferUsage::Uniform)) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (bits & static_cast<std::uint32_t>(BufferUsage::Storage)) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkBufferCreateInfo create{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    create.size = desc.size;
    create.usage = usage;
    create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Impl::BufferResource resource{};
    Check(vkCreateBuffer(d.device, &create, nullptr, &resource.buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(d.device, resource.buffer, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = d.FindMemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(d.device, &allocate, nullptr, &resource.memory), "vkAllocateMemory");
    Check(vkBindBufferMemory(d.device, resource.buffer, resource.memory, 0), "vkBindBufferMemory");
    if (!desc.initialData.empty()) {
        if (desc.initialData.size_bytes() > desc.size) throw std::invalid_argument("Buffer initial data exceeds its size");
        void* mapped{};
        Check(vkMapMemory(d.device, resource.memory, 0, desc.initialData.size_bytes(), 0, &mapped), "vkMapMemory");
        std::memcpy(mapped, desc.initialData.data(), desc.initialData.size_bytes());
        vkUnmapMemory(d.device, resource.memory);
    }
    const BufferHandle handle{d.nextBuffer++, 1};
    d.buffers.emplace(handle, resource);
    return handle;
}

ShaderModuleHandle VulkanRenderDevice::CreateShaderModule(const ShaderModuleDesc& desc) {
    if (desc.bytecode.empty() || desc.bytecode.size_bytes() % sizeof(std::uint32_t))
        throw std::invalid_argument("Vulkan shader bytecode must contain SPIR-V words");
    VkShaderModuleCreateInfo create{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create.codeSize = desc.bytecode.size_bytes();
    create.pCode = reinterpret_cast<const std::uint32_t*>(desc.bytecode.data());
    VkShaderModule shader{};
    Check(vkCreateShaderModule(m_impl->device, &create, nullptr, &shader), "vkCreateShaderModule");
    const ShaderModuleHandle handle{m_impl->nextShader++, 1};
    m_impl->shaders.emplace(handle, shader);
    return handle;
}

GraphicsPipelineHandle VulkanRenderDevice::CreatePipeline(const GraphicsPipelineDesc& desc) {
    auto& d = *m_impl;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = d.shaders.at(desc.vertexShader);
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = d.shaders.at(desc.fragmentShader);
    stages[1].pName = "main";
    std::vector<VkVertexInputBindingDescription> bindings;
    for (const auto& binding : desc.vertexLayout.bindings)
        bindings.push_back({binding.binding, binding.stride, binding.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto& attr : desc.vertexLayout.attributes)
        attributes.push_back({attr.location, attr.binding, ToVkVertexFormat(attr.format), attr.offset});
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = ToVkTopology(desc.topology);
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const VkDynamicState dynamics[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkPipelineLayoutCreateInfo layoutCreate{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    Impl::PipelineResource resource{};
    Check(vkCreatePipelineLayout(d.device, &layoutCreate, nullptr, &resource.layout), "vkCreatePipelineLayout");
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    const VkFormat colorFormat = desc.colorFormats.empty() ? d.surfaceFormat.format : ToVkFormat(desc.colorFormats.front());
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    create.pNext = &rendering;
    create.stageCount = 2;
    create.pStages = stages;
    create.pVertexInputState = &vertexInput;
    create.pInputAssemblyState = &assembly;
    create.pViewportState = &viewport;
    create.pRasterizationState = &raster;
    create.pMultisampleState = &multisample;
    create.pColorBlendState = &blend;
    create.pDynamicState = &dynamic;
    create.layout = resource.layout;
    create.renderPass = VK_NULL_HANDLE;
    Check(vkCreateGraphicsPipelines(d.device, VK_NULL_HANDLE, 1, &create, nullptr, &resource.pipeline), "vkCreateGraphicsPipelines");
    const GraphicsPipelineHandle handle{d.nextPipeline++, 1};
    d.pipelines.emplace(handle, resource);
    return handle;
}

std::unique_ptr<IRenderCommandList> VulkanRenderDevice::CreateCommandList(QueueType) { return std::make_unique<VulkanCommandList>(); }
void VulkanRenderDevice::Destroy(ImageHandle) { }
void VulkanRenderDevice::Destroy(ImageViewHandle) { }
void VulkanRenderDevice::Destroy(BufferHandle handle) {
    auto& d = *m_impl;
    const auto it = d.buffers.find(handle);
    if (it == d.buffers.end()) return;
    vkDestroyBuffer(d.device, it->second.buffer, nullptr);
    vkFreeMemory(d.device, it->second.memory, nullptr);
    d.buffers.erase(it);
}
void VulkanRenderDevice::Destroy(GraphicsPipelineHandle handle) {
    auto& d = *m_impl;
    const auto it = d.pipelines.find(handle);
    if (it == d.pipelines.end()) return;
    vkDestroyPipeline(d.device, it->second.pipeline, nullptr);
    vkDestroyPipelineLayout(d.device, it->second.layout, nullptr);
    d.pipelines.erase(it);
}
void VulkanRenderDevice::Destroy(ShaderModuleHandle handle) {
    auto& d = *m_impl;
    const auto it = d.shaders.find(handle);
    if (it == d.shaders.end()) return;
    vkDestroyShaderModule(d.device, it->second, nullptr);
    d.shaders.erase(it);
}
RenderCapabilities VulkanRenderDevice::GetCapabilities() const { return {}; }

void VulkanRenderDevice::Submit(const IRenderCommandList& list) {
    auto& d = *m_impl;
    const auto* commands = dynamic_cast<const VulkanCommandList*>(&list);
    if (!commands) throw std::invalid_argument("VulkanRenderDevice received a command list from another backend");
    Check(vkWaitForFences(d.device, 1, &d.inFlight, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    std::uint32_t imageIndex{};
    const auto acquire = vkAcquireNextImageKHR(d.device, d.swapchain, UINT64_MAX, d.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) Check(acquire, "vkAcquireNextImageKHR");
    Check(vkResetFences(d.device, 1, &d.inFlight), "vkResetFences");
    Check(vkResetCommandBuffer(d.commandBuffer, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(d.commandBuffer, &begin), "vkBeginCommandBuffer");
    VkImageMemoryBarrier2 toColor{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toColor.srcStageMask = d.imageInitialized[imageIndex] ? VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_2_NONE;
    toColor.srcAccessMask = d.imageInitialized[imageIndex] ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : 0;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = d.imageInitialized[imageIndex] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.image = d.swapchainImages[imageIndex];
    toColor.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(d.commandBuffer, &dependency);

    bool rendering{};
    for (const auto& command : commands->Commands()) std::visit([&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, VulkanCommandList::Begin>) {
            if (rendering) vkCmdEndRendering(d.commandBuffer);
            VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            color.imageView = d.swapchainViews[imageIndex];
            color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            color.loadOp = c.desc.clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            color.clearValue.color = {{c.desc.clearColorValue.x, c.desc.clearColorValue.y, c.desc.clearColorValue.z, 1.0f}};
            VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
            info.renderArea.offset = {std::max(c.desc.offsetX, 0), std::max(c.desc.offsetY, 0)};
            info.renderArea.extent = {c.desc.width ? c.desc.width : d.extent.width,
                                      c.desc.height ? c.desc.height : d.extent.height};
            info.layerCount = 1;
            info.colorAttachmentCount = 1;
            info.pColorAttachments = &color;
            vkCmdBeginRendering(d.commandBuffer, &info);
            VkViewport vp{static_cast<float>(info.renderArea.offset.x), static_cast<float>(info.renderArea.offset.y),
                          static_cast<float>(info.renderArea.extent.width), static_cast<float>(info.renderArea.extent.height), 0.0f, 1.0f};
            VkRect2D scissor = info.renderArea;
            vkCmdSetViewport(d.commandBuffer, 0, 1, &vp);
            vkCmdSetScissor(d.commandBuffer, 0, 1, &scissor);
            rendering = true;
        } else if constexpr (std::is_same_v<T, VulkanCommandList::End>) {
            if (rendering) { vkCmdEndRendering(d.commandBuffer); rendering = false; }
        } else if constexpr (std::is_same_v<T, VulkanCommandList::BindPipelineCmd>) {
            vkCmdBindPipeline(d.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, d.pipelines.at(c.handle).pipeline);
        } else if constexpr (std::is_same_v<T, VulkanCommandList::BindVertexCmd>) {
            const VkBuffer buffer = d.buffers.at(c.handle).buffer;
            constexpr VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(d.commandBuffer, 0, 1, &buffer, &offset);
        } else if constexpr (std::is_same_v<T, VulkanCommandList::BindIndexCmd>) {
            vkCmdBindIndexBuffer(d.commandBuffer, d.buffers.at(c.handle).buffer, 0, VK_INDEX_TYPE_UINT32);
        } else if constexpr (std::is_same_v<T, VulkanCommandList::DrawCmd>) {
            vkCmdDraw(d.commandBuffer, c.count, c.instances, c.first, c.firstInstance);
        } else if constexpr (std::is_same_v<T, VulkanCommandList::DrawIndexedCmd>) {
            vkCmdDrawIndexed(d.commandBuffer, c.count, 1, 0, 0, 0);
        } else if constexpr (std::is_same_v<T, VulkanCommandList::DispatchCmd>) {
            vkCmdDispatch(d.commandBuffer, c.x, c.y, c.z);
        } else if constexpr (std::is_same_v<T, VulkanCommandList::BindTextureCmd>) {
            throw std::runtime_error("Vulkan texture binding is not implemented in the triangle baseline");
        }
    }, command);
    if (rendering) vkCmdEndRendering(d.commandBuffer);
    VkImageMemoryBarrier2 toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image = d.swapchainImages[imageIndex];
    toPresent.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    dependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(d.commandBuffer, &dependency);
    Check(vkEndCommandBuffer(d.commandBuffer), "vkEndCommandBuffer");
    constexpr VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &d.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &d.commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &d.renderFinished;
    Check(vkQueueSubmit(d.queue, 1, &submit, d.inFlight), "vkQueueSubmit");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &d.renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &d.swapchain;
    present.pImageIndices = &imageIndex;
    const auto presented = vkQueuePresentKHR(d.queue, &present);
    if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR) Check(presented, "vkQueuePresentKHR");
    Check(vkQueueWaitIdle(d.queue), "vkQueueWaitIdle");
    d.imageInitialized[imageIndex] = true;
}

} // namespace Lighthouse
