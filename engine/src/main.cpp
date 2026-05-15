#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

[[nodiscard]] VkInstance create_instance() {
    std::uint32_t glfw_ext_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);

    VkApplicationInfo app{};
    app.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName   = "noted";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName        = "noted-engine";
    app.engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.apiVersion         = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = glfw_ext_count;
    ci.ppEnabledExtensionNames = glfw_exts;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return instance;
}

void print_devices(VkInstance instance) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    std::cout << "noted bootstrap: " << count << " Vulkan device(s)\n";
    for (std::uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devices[i], &props);
        std::cout << "  [" << i << "] " << props.deviceName
                  << " | API "
                  << VK_API_VERSION_MAJOR(props.apiVersion) << '.'
                  << VK_API_VERSION_MINOR(props.apiVersion) << '.'
                  << VK_API_VERSION_PATCH(props.apiVersion)
                  << " | driver " << props.driverVersion
                  << '\n';
    }
}

}  // namespace

int main() {
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "glfwInit failed\n";
        return EXIT_FAILURE;
    }
    if (glfwVulkanSupported() != GLFW_TRUE) {
        std::cerr << "Vulkan loader not available via GLFW\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    VkInstance instance = create_instance();
    if (instance == VK_NULL_HANDLE) {
        std::cerr << "vkCreateInstance failed\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    print_devices(instance);

    vkDestroyInstance(instance, nullptr);
    glfwTerminate();
    return EXIT_SUCCESS;
}
