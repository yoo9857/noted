// Application entry. The app layer is intentionally thin: wire up the
// engine, install observation hooks for the harness, run the main loop,
// shut everything down on the way out.

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "noted/engine/error/error.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"

namespace {

// Static feature flags so the harness picks them up at init.
noted::harness::FeatureFlag flag_enable_validation_layers{
    "gpu.enable_validation_layers", /*default=*/true};
noted::harness::Counter ctr_frames{"engine.frames"};

[[nodiscard]] auto create_instance() -> noted::Result<VkInstance> {
    std::uint32_t ext_count = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&ext_count);

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
    ci.enabledExtensionCount   = ext_count;
    ci.ppEnabledExtensionNames = exts;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed, "vkCreateInstance failed"));
    }
    return instance;
}

void log_devices(VkInstance instance) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(instance, &count, devs.data());

    std::cout << "noted bootstrap: " << count << " Vulkan device(s)\n";
    for (std::uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(devs[i], &props);
        std::cout << "  [" << i << "] " << props.deviceName
                  << " | API "
                  << VK_API_VERSION_MAJOR(props.apiVersion) << '.'
                  << VK_API_VERSION_MINOR(props.apiVersion) << '.'
                  << VK_API_VERSION_PATCH(props.apiVersion) << '\n';
    }
}

}  // namespace

int main() {
    // Install a default error observer so anything publishing on the error
    // channel surfaces during the bootstrap.
    auto& reg = noted::hook::registry();
    const auto err_sub = reg.on_error.subscribe(
        [](const noted::hook::ErrorObserved& e) {
            std::cerr << "[error] " << e.error.format() << '\n';
        });

    reg.on_startup.publish({});

    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "glfwInit failed\n";
        return EXIT_FAILURE;
    }
    if (glfwVulkanSupported() != GLFW_TRUE) {
        std::cerr << "Vulkan loader not available via GLFW\n";
        glfwTerminate();
        return EXIT_FAILURE;
    }

    auto instance = create_instance();
    if (!instance) {
        std::cerr << instance.error().format() << '\n';
        glfwTerminate();
        return EXIT_FAILURE;
    }

    log_devices(*instance);

    ctr_frames.add();  // demonstrate counters
    if (flag_enable_validation_layers) {
        std::cout << "validation layers requested\n";
    }

    vkDestroyInstance(*instance, nullptr);
    glfwTerminate();
    reg.on_shutdown.publish({});
    reg.on_error.unsubscribe(err_sub);
    return EXIT_SUCCESS;
}
