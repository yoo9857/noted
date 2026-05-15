// Application entry. Wires Engine + Window + GPU stack and runs the event
// loop. No rendering yet — record/submit/present land in feat/render-clear.

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>

#include "noted/engine/engine.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/surface.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/platform/window/window.hpp"

namespace {

noted::harness::FeatureFlag flag_validation_layers{
    "gpu.enable_validation_layers", /*default=*/true};
noted::harness::FeatureFlag flag_force_vsync{
    "gpu.force_vsync_fifo", /*default=*/false};

[[nodiscard]] auto glfw_required_extensions() -> std::span<const char* const> {
    std::uint32_t count = 0;
    const char** ptr = glfwGetRequiredInstanceExtensions(&count);
    return {ptr, static_cast<std::size_t>(count)};
}

void install_default_observers() {
    auto& reg = noted::hook::registry();
    (void)reg.on_error.subscribe([](const noted::hook::ErrorObserved& e) {
        std::cerr << "[error] " << e.error.format() << '\n';
    });
    (void)reg.on_frame_end.subscribe([](const noted::hook::FrameEnd& f) {
        if ((f.frame_index % 240) == 0) {
            std::cout << "frame " << f.frame_index
                      << " | cpu " << f.cpu_ms << " ms\n";
        }
    });
}

[[nodiscard]] auto create_window_surface(
    VkInstance              instance,
    noted::platform::Window& window) -> noted::Result<VkSurfaceKHR> {
    VkSurfaceKHR raw = VK_NULL_HANDLE;
    if (auto vr = glfwCreateWindowSurface(instance, window.native_handle(),
                                          nullptr, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_surface_lost,
            std::string{"glfwCreateWindowSurface failed: VkResult="} + std::to_string(vr)));
    }
    return raw;
}

}  // namespace

int main() {
    install_default_observers();

    noted::engine::Engine engine;
    if (auto r = engine.init(); !r) {
        std::cerr << r.error().format() << '\n';
        return EXIT_FAILURE;
    }

    auto window = noted::platform::Window::create({
        .title  = "noted",
        .width  = 1600,
        .height = 1000,
    });
    if (!window) {
        std::cerr << window.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto instance = noted::gpu::Instance::create({
        .app_name           = "noted",
        .app_version        = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .api_version        = VK_API_VERSION_1_3,
        .enable_validation  = static_cast<bool>(flag_validation_layers),
        .extra_extensions   = {},
        .surface_extensions = glfw_required_extensions(),
    });
    if (!instance) {
        std::cerr << instance.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto raw_surface = create_window_surface(instance->handle(), *window);
    if (!raw_surface) {
        std::cerr << raw_surface.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }
    auto surface = noted::gpu::Surface::adopt(*instance, *raw_surface);

    auto physical = noted::gpu::PhysicalDevice::select(*instance);
    if (!physical) {
        std::cerr << physical.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }
    std::cout << "selected GPU: " << physical->properties().deviceName << '\n';

    auto device = noted::gpu::Device::create(*physical, surface);
    if (!device) {
        std::cerr << device.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    const auto [fb_w, fb_h] = window->framebuffer_size();
    auto swapchain = noted::gpu::Swapchain::create(*physical, *device, surface,
        VkExtent2D{fb_w, fb_h},
        noted::gpu::SwapchainConfig{
            .desired_image_count = 3,
            .prefer_srgb         = true,
            .allow_mailbox       = !static_cast<bool>(flag_force_vsync),
            .force_fifo          = static_cast<bool>(flag_force_vsync),
        });
    if (!swapchain) {
        std::cerr << swapchain.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "swapchain: " << swapchain->summary().image_count
              << " images | "
              << swapchain->summary().extent.width << 'x'
              << swapchain->summary().extent.height
              << " | format=" << swapchain->summary().color_format
              << " | mode="   << swapchain->summary().present_mode << '\n';

    while (!window->should_close()) {
        engine.begin_frame();
        window->poll_events();
        // Record/submit/present lands in feat/render-clear.
        engine.end_frame();
    }

    device->wait_idle();
    (void)engine.shutdown();
    return EXIT_SUCCESS;
}
