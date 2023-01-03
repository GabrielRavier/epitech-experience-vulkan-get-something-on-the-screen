// Needed to get GLFW to work alongside Vulkan
#include <vulkan/vulkan_core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <vector>
#include <stdexcept>
#include <cstdint>

class VulkanSomethingOnTheScreenApp {
    static constexpr std::uint32_t windowWidth = 800;
    static constexpr std::uint32_t windowHeight = 600;
    static constexpr const char *name = "Get something on the screen with Vulkan";
    GLFWwindow *window;

    VkInstance vulkanInstance;
    
public:
    VulkanSomethingOnTheScreenApp()
    {
        // Initialize GLFW-related stuff
        {
            glfwInit();

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Needed to avoid GLFW creating an OpenGL context
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // Handling resizing is complicated with Vulkan so don't do it

            this->window = glfwCreateWindow(this->windowWidth, this->windowHeight, this->name, nullptr, nullptr);
        }


        // Initialize Vulkan-related stuff
        {
            // Initialize Vulkan instance
            {
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
                    for (const auto &i : extensions)
                        std::cout << '\t' << i.extensionName << '\n';
                }

                // GLFW requires certain vulkan extensions, so this gets and enables them
                std::uint32_t glfwProvidedRequiredExtensionCount;
                const char **glfwProvidedRequiredExtensions = glfwGetRequiredInstanceExtensions(&glfwProvidedRequiredExtensionCount);

                createInfo.enabledExtensionCount = glfwProvidedRequiredExtensionCount;
                createInfo.ppEnabledExtensionNames = glfwProvidedRequiredExtensions;

                // We don't want to enable global validation layers for now
                createInfo.enabledLayerCount = 0;

                if (vkCreateInstance(&createInfo, nullptr, &this->vulkanInstance) != VK_SUCCESS)
                    throw std::runtime_error("Failed to create Vulkan instance");
            }
        }
    }

    ~VulkanSomethingOnTheScreenApp()
    {
        vkDestroyInstance(this->vulkanInstance, nullptr);
        glfwDestroyWindow(this->window);
        glfwTerminate();
    }
    
    void run() {
        while (!glfwWindowShouldClose(this->window))
            glfwPollEvents();
    }
};

// We'll do our error handling mostly by just throwing exceptions, so leave a top-level wrapper here to catch any exceptions that occur
int main() {
    try {
        VulkanSomethingOnTheScreenApp().run();
    } catch (const std::exception &exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
