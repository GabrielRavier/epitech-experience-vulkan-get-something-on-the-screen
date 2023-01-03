// Needed to get GLFW to work alongside Vulkan
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <string>
#include <vector>
#include <stdexcept>
#include <array>
#include <optional>
#include <exception>
#include <algorithm>
#include <limits>
#include <cstring>
#include <cstdint>

[[nodiscard]] inline std::string readFullFile(std::string_view fileName)
{
    const std::ifstream fileStream(fileName.data());

    std::string fileContents = (std::stringstream() << fileStream.rdbuf()).str();
    if (fileStream.fail())
        throw std::runtime_error("Failure to read from " + std::string(fileName));

    return fileContents;
}

static VkResult internalVkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void internalVkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
        func(instance, debugMessenger, pAllocator);
}

class vulkanSomethingOnTheScreenApp {
    static constexpr std::uint32_t windowWidth = 800;
    static constexpr std::uint32_t windowHeight = 600;
    static constexpr const char *name = "Get something on the screen with Vulkan";
    GLFWwindow *glfwWindow;

    static constexpr std::array<const char *, 1> validationLayers = {
        {
            "VK_LAYER_KHRONOS_validation",
        }
    };
    VkInstance vulkanInstance = VK_NULL_HANDLE; // We'll need a connection to the Vulkan library
    
    VkSurfaceKHR vulkanSurface = VK_NULL_HANDLE; // We'll need a surface to interact with the window system. Note: this is an extension but we've enabled it through glfwGetRequiredInstanceExtensions

    VkDebugUtilsMessengerEXT vulkanDebugMessenger = VK_NULL_HANDLE; // We'll need a handle for the Vulkan debugging messages callback

    VkPhysicalDevice vulkanPhysicalDevice = VK_NULL_HANDLE; // We'll need a handle to the GPU we're gonna use

    // We'll need the KHR swapchain device extension to present images to the screen
    static constexpr std::array<const char *, 1> requiredVulkanDeviceExtensions = {
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        }
    };
    VkDevice vulkanDevice = VK_NULL_HANDLE; // We'll need a handle to a "logical device" to interface with our physical device

    VkQueue vulkanGraphicsQueue = VK_NULL_HANDLE;
    VkQueue vulkanPresentQueue = VK_NULL_HANDLE;
    
    VkSwapchainKHR vulkanSwapChain = VK_NULL_HANDLE;
    std::vector<VkImage> vulkanSwapChainImages;
    VkFormat vulkanSwapChainImageFormat;
    VkExtent2D vulkanSwapChainExtent;
    VkPipelineLayout vulkanPipelineLayout;

    std::vector<VkImageView> vulkanSwapChainImageViews;

    VkRenderPass vulkanRenderPass;
    VkPipeline vulkanGraphicsPipeline;

    std::vector<VkFramebuffer> vulkanSwapChainFramebuffers;

    VkCommandPool vulkanCommandPool;
    VkCommandBuffer vulkanCommandBuffer;

    VkSemaphore vulkanImageAvailableSemaphore;
    VkSemaphore vulkanRenderFinishedSemaphore;
    VkFence vulkanInFlightFence;
    
public:
    vulkanSomethingOnTheScreenApp()
    {
        // Initialize GLFW-related stuff
        {
            glfwInit();

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Needed to avoid GLFW creating an OpenGL context
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // Handling resizing is complicated with Vulkan so don't do it

            this->glfwWindow = glfwCreateWindow(this->windowWidth, this->windowHeight, this->name, nullptr, nullptr);
        }

        // Initialize Vulkan-related stuff
        {
            // Initialize Vulkan instance
            {
                if (!this->areVulkanValidationLayersSupported())
                    throw std::runtime_error("Wanted Vulkan validation layers, but they were not available !");

                VkApplicationInfo appInfo = {};
                appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
                
                appInfo.pApplicationName = this->name;
                appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.pEngineName = "Does not use an engine";
                appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
                appInfo.apiVersion = VK_API_VERSION_1_0;

                VkInstanceCreateInfo createInfo = {};
                createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
                
                createInfo.pApplicationInfo = &appInfo;

                // For diagnostics purposes
                {
                    std::uint32_t extensionCount = 0;
                    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

                    std::vector<VkExtensionProperties> extensions;
                    extensions.resize(extensionCount);

                    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

                    std::cout << "Available Vulkan extensions:\n";
                    for (const auto &extension : extensions)
                        std::cout << '\t' << extension.extensionName << '\n';
                }

                auto requiredExtensions = this->getRequiredVulkanExtensions();
                createInfo.enabledExtensionCount = static_cast<std::uint32_t>(requiredExtensions.size());
                createInfo.ppEnabledExtensionNames = requiredExtensions.data();

                createInfo.enabledLayerCount = static_cast<std::uint32_t>(this->validationLayers.size());
                createInfo.ppEnabledLayerNames = this->validationLayers.data();

                // We also want to be able to debug issues in vkCreateInstance, if any
                auto debugMessengerCreateInfo = this->makeDebugMessengerCreateInfo();
                createInfo.pNext = &debugMessengerCreateInfo;

                if (vkCreateInstance(&createInfo, nullptr, &this->vulkanInstance) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create Vulkan instance");
            }

            // Setup debug messenger
            {
                auto createInfo = this->makeDebugMessengerCreateInfo();
                if (internalVkCreateDebugUtilsMessengerEXT(this->vulkanInstance, &createInfo, nullptr, &this->vulkanDebugMessenger) != VK_SUCCESS)
                    throw std::runtime_error("Failed to set up Vulkan debug messenger");
            }

            // Create a surface
            {
                if (glfwCreateWindowSurface(this->vulkanInstance, this->glfwWindow, nullptr, &this->vulkanSurface) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create window surface");
            }

            // Pick a physical device
            {
                std::uint32_t physicalDeviceCount = 0;
                vkEnumeratePhysicalDevices(this->vulkanInstance, &physicalDeviceCount, nullptr);

                // No point in going further if no GPUs have Vulkan suport
                if (physicalDeviceCount == 0)
                    throw std::runtime_error("Failed to find a GPU with Vulkan support");

                std::vector<VkPhysicalDevice> physicalDevices;
                physicalDevices.resize(physicalDeviceCount);
                vkEnumeratePhysicalDevices(this->vulkanInstance, &physicalDeviceCount, physicalDevices.data());

                for (const auto &physicalDevice : physicalDevices)
                    if (this->isVulkanPhysicalDeviceSuitableForUs(physicalDevice)) {
                        this->vulkanPhysicalDevice = physicalDevice;
                        break;
                    }

                if (this->vulkanPhysicalDevice == VK_NULL_HANDLE)
                    throw std::runtime_error("Failed to find a GPU with Vulkan support that is suitable for us");
            }

            // Create a logical device
            {
                auto familyIndices = this->findVulkanQueueFamilies(this->vulkanPhysicalDevice);

                std::vector<VkDeviceQueueCreateInfo> deviceQueueCreateInfos;
                std::unordered_set<std::uint32_t> uniqueQueueFamilyIndices = {
                    familyIndices.graphicsFamily.value(),
                    familyIndices.presentFamily.value(),
                };

                float queuePriority = 1.f;
                for (auto queueFamilyIndex : uniqueQueueFamilyIndices) {
                    VkDeviceQueueCreateInfo deviceQueueCreateInfo = {};
                    deviceQueueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;

                    deviceQueueCreateInfo.queueFamilyIndex = queueFamilyIndex;
                    deviceQueueCreateInfo.queueCount = 1;
                    deviceQueueCreateInfo.pQueuePriorities = &queuePriority;
                    deviceQueueCreateInfos.push_back(deviceQueueCreateInfo);
                }

                VkPhysicalDeviceFeatures physicalDeviceFeatures = {};

                VkDeviceCreateInfo deviceCreateInfo = {};
                deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

                deviceCreateInfo.queueCreateInfoCount = static_cast<std::uint32_t>(deviceQueueCreateInfos.size());
                deviceCreateInfo.pQueueCreateInfos = deviceQueueCreateInfos.data();
                deviceCreateInfo.pEnabledFeatures = &physicalDeviceFeatures;

                // Using swapchains requires us to enable the VK_KHR_swapchain extension here
                deviceCreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(this->requiredVulkanDeviceExtensions.size());
                deviceCreateInfo.ppEnabledExtensionNames = this->requiredVulkanDeviceExtensions.data();

                deviceCreateInfo.enabledLayerCount = static_cast<std::uint32_t>(this->validationLayers.size());
                deviceCreateInfo.ppEnabledLayerNames = this->validationLayers.data();

                if (vkCreateDevice(this->vulkanPhysicalDevice, &deviceCreateInfo, nullptr, &this->vulkanDevice) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create logical device");

                vkGetDeviceQueue(this->vulkanDevice, familyIndices.graphicsFamily.value(), 0, &this->vulkanGraphicsQueue);
                vkGetDeviceQueue(this->vulkanDevice, familyIndices.presentFamily.value(), 0, &this->vulkanPresentQueue);
            }

            // Create a swap chain
            {
                auto swapChainSupport = this->queryVulkanSwapChainSupport(this->vulkanPhysicalDevice);
                auto surfaceFormat = this->chooseVulkanSwapSurfaceFormat(swapChainSupport.surfaceFormats);
                auto presentMode = this->chooseVulkanSwapPresentMode(swapChainSupport.presentModes);
                auto extent = this->chooseVulkanSwapExtent(swapChainSupport.capabilities);

                // Sticking to the required minimum image count might mean we'd have to wait on the driver to complete internal operations before it could acquire other images to render, so it's recommended to request at least one more image than the minimum (while making sure that doesn't exceed the maximum either (note: maxImageCount == 0 means there is no maximum))
                std::uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
                if (swapChainSupport.capabilities.maxImageCount != 0)
                    imageCount = std::min(imageCount, swapChainSupport.capabilities.maxImageCount);

                VkSwapchainCreateInfoKHR createInfo = {};
                createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

                createInfo.surface = this->vulkanSurface;

                createInfo.minImageCount = imageCount;
                createInfo.imageFormat = surfaceFormat.format;
                createInfo.imageColorSpace = surfaceFormat.colorSpace;
                createInfo.imageExtent = extent;
                createInfo.imageArrayLayers = 1; // We're not developing a stereoscopic 3D application lol
                createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

                // VK_SHARING_MODE_EXCLUSIVE has the best performance, so use it when possible (i.e. when we have the same graphics and presenting family indices). Apparently it's also possible to do otherwise but I'm not gonna try to do EVEN MORE stuff just to handle that
                auto familyIndices = this->findVulkanQueueFamilies(this->vulkanPhysicalDevice);
                std::array<std::uint32_t, 2> queueFamilyIndices = {
                    {
                        familyIndices.graphicsFamily.value(),
                        familyIndices.presentFamily.value(),
                    }
                };

                if (familyIndices.graphicsFamily.value() != familyIndices.presentFamily.value()) {
                    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                    createInfo.queueFamilyIndexCount = 2;
                    createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
                } else {
                    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
                    createInfo.queueFamilyIndexCount = 0;
                    createInfo.pQueueFamilyIndices = nullptr;
                }

                // We need to do this to make sure there's no pre-transform at all
                createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

                // We're very likely to want to ignore the alpha channel
                createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

                createInfo.presentMode = presentMode;
                createInfo.clipped = VK_TRUE; // We don't want to read back pixels that were obscured or anything like that, so allowing clipping is fine

                // We assume we'll only ever create one swap chain because handling it is otherwise a complete mess (this is also why we don't support stuff like resizing btw, and yes that'd require making a new swap chain lol)
                createInfo.oldSwapchain = VK_NULL_HANDLE;

                if (vkCreateSwapchainKHR(this->vulkanDevice, &createInfo, nullptr, &this->vulkanSwapChain) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create swap chain");

                vkGetSwapchainImagesKHR(this->vulkanDevice, this->vulkanSwapChain, &imageCount, nullptr);
                this->vulkanSwapChainImages.resize(imageCount);
                vkGetSwapchainImagesKHR(this->vulkanDevice, this->vulkanSwapChain, &imageCount, this->vulkanSwapChainImages.data());

                // Needed for swap chain image view creation
                this->vulkanSwapChainImageFormat = surfaceFormat.format;
                this->vulkanSwapChainExtent = extent;
            }

            // Create swap chain image views
            {
                this->vulkanSwapChainImageViews.resize(this->vulkanSwapChainImages.size());

                for (std::size_t i = 0; i < this->vulkanSwapChainImages.size(); ++i) {
                    VkImageViewCreateInfo createInfo = {};
                    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    
                    createInfo.image = this->vulkanSwapChainImages.at(i);

                    // We want to use a 2D texture
                    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    createInfo.format = this->vulkanSwapChainImageFormat;

                    // We don't want to do any fancy stuff with the colors, so just stick with the default mappings
                    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
                    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

                    // We need to specify the image's purpose and what part of the image should be accessed - we want to use them as color targets without any mipmapping or layering
                    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    createInfo.subresourceRange.baseMipLevel = 0;
                    createInfo.subresourceRange.levelCount = 1;
                    createInfo.subresourceRange.baseArrayLayer = 0;
                    createInfo.subresourceRange.layerCount = 1;

                    if (vkCreateImageView(this->vulkanDevice, &createInfo, nullptr, &this->vulkanSwapChainImageViews.at(i)) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create one of the image views");
                }
            }

            // Create render pass
            {
                VkAttachmentDescription colorAttachmentDescription = {};
                colorAttachmentDescription.format = this->vulkanSwapChainImageFormat;
                colorAttachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;

                colorAttachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                colorAttachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

                colorAttachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                colorAttachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

                colorAttachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                colorAttachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                VkAttachmentReference colorAttachmentReference = {};
                colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkSubpassDescription subpassDescription = {};
                subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

                subpassDescription.colorAttachmentCount = 1;
                subpassDescription.pColorAttachments = &colorAttachmentReference;

                VkRenderPassCreateInfo renderPassCreateInfo = {};
                renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                renderPassCreateInfo.attachmentCount = 1;
                renderPassCreateInfo.pAttachments = &colorAttachmentDescription;
                renderPassCreateInfo.subpassCount = 1;
                renderPassCreateInfo.pSubpasses = &subpassDescription;

                VkSubpassDependency subpassDependency = {};
                subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL; // The implicit subpass before the render pass
                subpassDependency.dstSubpass = 0; // Our pass, the first and only one
                subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // We need to wait for the swap chain to finish reading from the image before we can access it
                subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // Prevent the transition from happening until it's actually necessary (and allowed)
                subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

                renderPassCreateInfo.dependencyCount = 1;
                renderPassCreateInfo.pDependencies = &subpassDependency;

                if (vkCreateRenderPass(this->vulkanDevice, &renderPassCreateInfo, nullptr, &this->vulkanRenderPass) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create render pass");
            }

            // Create graphics pipeline
            {
                auto vertShaderCode = readFullFile("./shaders/vert.spv");
                auto fragShaderCode = readFullFile("./shaders/frag.spv");

                auto vertShaderModule = this->createVulkanShaderModuleFromCode(vertShaderCode);
                auto fragShaderModule = this->createVulkanShaderModuleFromCode(fragShaderCode);

                VkPipelineShaderStageCreateInfo vertShaderStageCreateInfo = {};
                vertShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

                vertShaderStageCreateInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;

                vertShaderStageCreateInfo.module = vertShaderModule;
                vertShaderStageCreateInfo.pName = "main";

                VkPipelineShaderStageCreateInfo fragShaderStageCreateInfo = {};
                fragShaderStageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

                fragShaderStageCreateInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

                fragShaderStageCreateInfo.module = fragShaderModule;
                fragShaderStageCreateInfo.pName = "main";

                std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {
                    {
                        vertShaderStageCreateInfo,
                        fragShaderStageCreateInfo,
                    }
                };

                VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = {};
                vertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

                VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = {};
                inputAssemblyStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

                // We intend to draw triangles
                inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;

                VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
                viewportStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

                // We do not need to specify the actual viewport/scissor rectangles, we'll do that at drawing time
                viewportStateCreateInfo.viewportCount = 1;
                viewportStateCreateInfo.scissorCount = 1;

                VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = {};
                rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

                // Using any mode other than VK_POLYGON_MODE_FILL would require enabling a corresponding GPU feature so just use it as the default 
                rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;

                rasterizationStateCreateInfo.lineWidth = 1.f;
                
                rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
                rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;

                // Just disable multisampling
                VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = {};
                multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

                multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
                multisampleStateCreateInfo.minSampleShading = 1.f;

                VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
                colorBlendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;

                VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = {};
                colorBlendStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlendStateCreateInfo.logicOp = VK_LOGIC_OP_COPY;
                colorBlendStateCreateInfo.attachmentCount = 1;
                colorBlendStateCreateInfo.pAttachments = &colorBlendAttachmentState;

                VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
                dynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;

                // We need to enable dynamic states for the stuff we want to use dynamically
                std::array<VkDynamicState, 2> dynamicStates = {
                    {
                        VK_DYNAMIC_STATE_VIEWPORT,
                        VK_DYNAMIC_STATE_SCISSOR,
                    }
                };
                dynamicStateCreateInfo.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
                dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();

                VkPipelineLayoutCreateInfo layoutCreateInfo = {};
                layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

                if (vkCreatePipelineLayout(this->vulkanDevice, &layoutCreateInfo, nullptr, &this->vulkanPipelineLayout) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create pipeline layout");

                VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {};
                graphicsPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;

                graphicsPipelineCreateInfo.stageCount = 2;
                graphicsPipelineCreateInfo.pStages = shaderStages.data();

                // Fixed-function stage description
                graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
                graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
                graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
                graphicsPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
                graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
                graphicsPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
                graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
                
                graphicsPipelineCreateInfo.layout = this->vulkanPipelineLayout;

                graphicsPipelineCreateInfo.renderPass = this->vulkanRenderPass;
                graphicsPipelineCreateInfo.subpass = 0;

                // We don't want to derive from any base pipeline
                graphicsPipelineCreateInfo.basePipelineIndex = -1;

                if (vkCreateGraphicsPipelines(this->vulkanDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCreateInfo, nullptr, &this->vulkanGraphicsPipeline) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create graphics pipeline");
                
                vkDestroyShaderModule(this->vulkanDevice, fragShaderModule, nullptr);
                vkDestroyShaderModule(this->vulkanDevice, vertShaderModule, nullptr);
            }

            // Create framebuffers
            {
                this->vulkanSwapChainFramebuffers.resize(this->vulkanSwapChainImageViews.size());

                for (std::size_t i = 0; i < this->vulkanSwapChainImageViews.size(); ++i) {
                    VkFramebufferCreateInfo framebufferCreateInfo = {};
                    framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;

                    framebufferCreateInfo.renderPass = this->vulkanRenderPass;
                    framebufferCreateInfo.attachmentCount = 1;
                    framebufferCreateInfo.pAttachments = &this->vulkanSwapChainImageViews[i];
                    framebufferCreateInfo.width = this->vulkanSwapChainExtent.width;
                    framebufferCreateInfo.height = this->vulkanSwapChainExtent.height;
                    framebufferCreateInfo.layers = 1;

                    if (vkCreateFramebuffer(this->vulkanDevice, &framebufferCreateInfo, nullptr, &this->vulkanSwapChainFramebuffers[i]) != VK_SUCCESS)
                        throw std::runtime_error("Failed to create framebuffer");
                }
            }

            // Create command pool
            {
                VkCommandPoolCreateInfo commandPoolCreateInfo = {};
                commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

                // We're recording a command buffer every frame, so we want to be able to reset and rerecord over it
                commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

                commandPoolCreateInfo.queueFamilyIndex = this->findVulkanQueueFamilies(this->vulkanPhysicalDevice).graphicsFamily.value();

                if (vkCreateCommandPool(this->vulkanDevice, &commandPoolCreateInfo, nullptr, &this->vulkanCommandPool) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create command pool");
            }

            // Create command buffer
            {
                VkCommandBufferAllocateInfo allocateInfo = {};
                allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

                allocateInfo.commandPool = this->vulkanCommandPool;
                allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // We're not going to use the secondary command buffer functionality
                allocateInfo.commandBufferCount = 1;

                if (vkAllocateCommandBuffers(this->vulkanDevice, &allocateInfo, &this->vulkanCommandBuffer) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create command buffer");
            }

            // Create sync objects
            {
                VkSemaphoreCreateInfo semaphoreCreateInfo = {};
                semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

                VkFenceCreateInfo fenceCreateInfo = {};
                fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // The fence is already in the signalled state so that we don't need special handling in drawFrame and can just wait on it even on the first frame

                if (vkCreateSemaphore(this->vulkanDevice, &semaphoreCreateInfo, nullptr, &this->vulkanImageAvailableSemaphore) != VK_SUCCESS ||
                    vkCreateSemaphore(this->vulkanDevice, &semaphoreCreateInfo, nullptr, &this->vulkanRenderFinishedSemaphore) != VK_SUCCESS ||
                    vkCreateFence(this->vulkanDevice, &fenceCreateInfo, nullptr, &this->vulkanInFlightFence) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create semaphores and fence");
            }
        }
    }

    ~vulkanSomethingOnTheScreenApp()
    {
        vkDestroyFence(this->vulkanDevice, this->vulkanInFlightFence, nullptr);
        vkDestroySemaphore(this->vulkanDevice, this->vulkanRenderFinishedSemaphore, nullptr);
        vkDestroySemaphore(this->vulkanDevice, this->vulkanImageAvailableSemaphore, nullptr);
        
        vkDestroyCommandPool(this->vulkanDevice, this->vulkanCommandPool, nullptr);
        
        for (auto swapChainFramebuffer : this->vulkanSwapChainFramebuffers)
            vkDestroyFramebuffer(this->vulkanDevice, swapChainFramebuffer, nullptr);
        
        vkDestroyPipeline(this->vulkanDevice, this->vulkanGraphicsPipeline, nullptr);
        vkDestroyPipelineLayout(this->vulkanDevice, this->vulkanPipelineLayout, nullptr);

        vkDestroyRenderPass(this->vulkanDevice, this->vulkanRenderPass, nullptr);
        
        for (auto vulkanSwapChainImageView : this->vulkanSwapChainImageViews)
            vkDestroyImageView(this->vulkanDevice, vulkanSwapChainImageView, nullptr);
        
        vkDestroySwapchainKHR(this->vulkanDevice, this->vulkanSwapChain, nullptr);

        vkDestroyDevice(this->vulkanDevice, nullptr);

        vkDestroySurfaceKHR(this->vulkanInstance, this->vulkanSurface, nullptr);
        
        internalVkDestroyDebugUtilsMessengerEXT(this->vulkanInstance, this->vulkanDebugMessenger, nullptr);

        vkDestroyInstance(this->vulkanInstance, nullptr);

        glfwDestroyWindow(this->glfwWindow);
        glfwTerminate();
    }

    bool areVulkanValidationLayersSupported()
    {
        std::uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers;
        availableLayers.resize(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        // Check whether all of our required validation layers are available
        for (const char *layerName : this->validationLayers) {
            bool wasLayerFound = false;

            for (const auto &layerProperties : availableLayers) {
                if (std::strcmp(layerName, layerProperties.layerName) == 0) {
                    wasLayerFound = true;
                    break;
                }
            }

            if (!wasLayerFound)
                return false;
        }

        return true;
    }

    std::vector<const char *> getRequiredVulkanExtensions()
    {
        // GLFW requires certain Vulkan extensions
        std::uint32_t glfwProvidedRequiredExtensionCount;
        const char **glfwProvidedRequiredExtensions = glfwGetRequiredInstanceExtensions(&glfwProvidedRequiredExtensionCount);

        std::vector<const char *> extensions(glfwProvidedRequiredExtensions, glfwProvidedRequiredExtensions + glfwProvidedRequiredExtensionCount);

        // We require the debug messenger extension
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        return extensions;
    }

    static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData)
    {
        if (messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            std::cerr << "Vulkan validation layer ERROR: ";
        else
            std::cerr << "Vulkan validation layer: ";
        std::cerr << pCallbackData->pMessageIdName << ": " << pCallbackData->pMessage << '\n';
        return VK_FALSE;
    }

    VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerCreateInfo()
    {
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = &vulkanSomethingOnTheScreenApp::vulkanDebugCallback;
        createInfo.pUserData = nullptr;

        return createInfo;
    }

    // Not all physical devices are created equal. This function judges a physical device to determine its worthiness w.r.t. the operations we want to do.
    bool isVulkanPhysicalDeviceSuitableForUs(VkPhysicalDevice physicalDevice)
    {
        auto familyIndices = this->findVulkanQueueFamilies(physicalDevice);
        if (!familyIndices.isComplete())
            return false;

        if (!this->doesVulkanDeviceHaveAdequateExtensionSupport(physicalDevice))
            return false;

        auto swapChainSupport = this->queryVulkanSwapChainSupport(physicalDevice);
        if (swapChainSupport.surfaceFormats.empty() || swapChainSupport.presentModes.empty())
            return false;

        return true;
    }

    // Contains the indices for all the queue families we need
    struct vulkanQueueFamilyIndices {
        std::optional<std::uint32_t> graphicsFamily;
        std::optional<std::uint32_t> presentFamily;

        bool isComplete() const
        {
            return this->graphicsFamily.has_value() && this->presentFamily.has_value();
        }
    };

    // We need to check which queue families are supported by our device and which one supports the commands we want to use.
    // This looks for all the queue families we need
    vulkanQueueFamilyIndices findVulkanQueueFamilies(VkPhysicalDevice physicalDevice)
    {
        vulkanQueueFamilyIndices result;

        std::uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies;
        queueFamilies.resize(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        {
            std::uint32_t i = 0;
            for (const auto &queueFamily : queueFamilies) {
                if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                    result.graphicsFamily = i;

                VkBool32 presentSupport = false;
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, this->vulkanSurface, &presentSupport);

                if (presentSupport)
                    result.presentFamily = i;
            
                if (result.isComplete())
                    break;
                ++i;
            }
        }

        return result;
    }

    // We'll need to check whether all our required extensions are supported by the device
    bool doesVulkanDeviceHaveAdequateExtensionSupport(VkPhysicalDevice physicalDevice)
    {
        std::uint32_t extensionCount;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions;
        availableExtensions.resize(extensionCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

        std::unordered_set<std::string> requiredExtensions(this->requiredVulkanDeviceExtensions.begin(), this->requiredVulkanDeviceExtensions.end());
        for (const auto &extension : availableExtensions)
            requiredExtensions.erase(extension.extensionName);
        return requiredExtensions.empty();
    }

    struct vulkanSwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> surfaceFormats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    vulkanSwapChainSupportDetails queryVulkanSwapChainSupport(VkPhysicalDevice physicalDevice)
    {
        vulkanSwapChainSupportDetails result;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, this->vulkanSurface, &result.capabilities);

        std::uint32_t surfaceFormatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, this->vulkanSurface, &surfaceFormatCount, nullptr);

        if (surfaceFormatCount != 0) {
            result.surfaceFormats.resize(surfaceFormatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, this->vulkanSurface, &surfaceFormatCount, result.surfaceFormats.data());
        }

        std::uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->vulkanSurface, &presentModeCount, nullptr);

        if (presentModeCount != 0) {
            result.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->vulkanSurface, &presentModeCount, result.presentModes.data());
        }
        
        return result;
    }

    VkSurfaceFormatKHR chooseVulkanSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats)
    {
        // Use SRGB if it is available as it gives more accurate perceived colors: https://stackoverflow.com/questions/12524623/what-are-the-practical-differences-when-working-with-colors-in-a-linear-vs-a-no
        for (const auto &format : availableFormats)
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
                format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return format;
        return availableFormats.at(0);
    }

    VkPresentModeKHR chooseVulkanSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes)
    {
        // Triple buffering is nice, so use VK_PRESENT_MODE_MAILBOX_KHR if possible
        for (const auto &presentMode : availablePresentModes)
            if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
                return presentMode;
        
        // Only VK_PRESENT_MODE_FIFO_KHR is guaranteed to be available, so use it as a fallback
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D chooseVulkanSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
    {
        // Vulkan tells us to match the resolution of the window by setting the width/height to currentExtent, but some window managers allow us to differ and they do this by setting the width and height of currentExtent to the maximum 32-bit value
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max() &&
            capabilities.currentExtent.height != std::numeric_limits<std::uint32_t>::max())
            return capabilities.currentExtent;

        // In the case where we can differ from the resolution for the window, try to pick the resolution that best matches the window within the Vulkan-provided bounds
        // (Note: we don't use the GLFW-provided coordinates directly because they sometimes do not correspond to pixels (for example, on high DPI displays)
        int width, height;
        glfwGetFramebufferSize(this->glfwWindow, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        return actualExtent;
    }

    VkShaderModule createVulkanShaderModuleFromCode(std::string_view code)
    {
        VkShaderModuleCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const std::uint32_t *>(code.data());

        VkShaderModule result;
        if (vkCreateShaderModule(this->vulkanDevice, &createInfo, nullptr, &result) != VK_SUCCESS)
            throw std::runtime_error("Failed to create shader module");
        return result;
    }

    void recordVulkanCommandBuffer(VkCommandBuffer commandBuffer, std::uint32_t imageIndex)
    {
        VkCommandBufferBeginInfo commandBufferBeginInfo = {};
        commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin recording command buffer");

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

        renderPassBeginInfo.renderPass = this->vulkanRenderPass;
        renderPassBeginInfo.framebuffer = this->vulkanSwapChainFramebuffers.at(imageIndex);

        renderPassBeginInfo.renderArea.extent = this->vulkanSwapChainExtent;

        // We clear the screen with completely black black
        VkClearValue clearColor = {{{0.f, 0.f, 0.f, 1.f}}};
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;

        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo,
                             VK_SUBPASS_CONTENTS_INLINE // We're not using secondary command buffers
        );

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, this->vulkanGraphicsPipeline);

        // As we set the viewport and scissor state for the pipeline to be dynamic, we need to set them in the command buffer before drawing
        VkViewport viewport = {};
        viewport.width = static_cast<float>(this->vulkanSwapChainExtent.width);
        viewport.height = static_cast<float>(this->vulkanSwapChainExtent.height);
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.extent = this->vulkanSwapChainExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Finally !!!!
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);

        vkCmdEndRenderPass(commandBuffer);

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to record command buffer");
    }

    void run()
    {
        while (!glfwWindowShouldClose(this->glfwWindow)) {
            glfwPollEvents();
            this->drawFrame();
        }

        // We need to wait for the logical device to finish all its operations since otherwise all of the resources we're using will still be in use when we try to destroy them
        vkDeviceWaitIdle(this->vulkanDevice);
    }

    void drawFrame()
    {
        vkWaitForFences(this->vulkanDevice, 1, &this->vulkanInFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(this->vulkanDevice, 1, &this->vulkanInFlightFence); // We need to manually reset the fence back to the unsignalled state

        std::uint32_t imageIndex;
        vkAcquireNextImageKHR(this->vulkanDevice, this->vulkanSwapChain, UINT64_MAX, this->vulkanImageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
 
        vkResetCommandBuffer(this->vulkanCommandBuffer, 0);

        this->recordVulkanCommandBuffer(this->vulkanCommandBuffer, imageIndex);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // We want the execution to wait until writing colors to the image is available
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &this->vulkanImageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = &waitStage;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &this->vulkanCommandBuffer;

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &this->vulkanRenderFinishedSemaphore;

        if (vkQueueSubmit(this->vulkanGraphicsQueue, 1, &submitInfo, this->vulkanInFlightFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit draw command buffer");

        VkPresentInfoKHR presentInfoKHR = {};
        presentInfoKHR.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfoKHR.waitSemaphoreCount = 1;
        presentInfoKHR.pWaitSemaphores = &this->vulkanRenderFinishedSemaphore;

        presentInfoKHR.swapchainCount = 1;
        presentInfoKHR.pSwapchains = &this->vulkanSwapChain;
        presentInfoKHR.pImageIndices = &imageIndex;

        vkQueuePresentKHR(this->vulkanPresentQueue, &presentInfoKHR);
    }
};

// We'll do our error handling mostly by just throwing exceptions, so leave a top-level wrapper here to catch any exceptions that occur
int main()
{
    try {
        vulkanSomethingOnTheScreenApp().run();
    } catch (const std::exception &exception) {
        std::cerr << "Error (stdexcept): " << exception.what() << '\n';
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "WTF ?????? (abnormal exception type)\n";
    }
    return EXIT_SUCCESS;
}
