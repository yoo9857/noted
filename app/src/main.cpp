// Application entry. The app layer is intentionally thin: spin up the
// Engine, open a Window, create the GPU Instance + pick a PhysicalDevice,
// then run the event loop until the window closes.

#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include "noted/engine/engine.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/platform/window/window.hpp"

namespace {

noted::harness::FeatureFlag flag_validation_layers{
    "gpu.enable_validation_layers", /*default=*/true};

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

    const auto surface_exts = glfw_required_extensions();
    auto instance = noted::gpu::Instance::create({
        .app_name           = "noted",
        .app_version        = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .api_version        = VK_API_VERSION_1_3,
        .enable_validation  = static_cast<bool>(flag_validation_layers),
        .extra_extensions   = {},
        .surface_extensions = surface_exts,
    });
    if (!instance) {
        std::cerr << instance.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto picked = noted::gpu::PhysicalDevice::select(*instance);
    if (!picked) {
        std::cerr << picked.error().format() << '\n';
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    std::cout << "selected GPU: " << picked->properties().deviceName
              << " | API "
              << VK_API_VERSION_MAJOR(picked->properties().apiVersion) << '.'
              << VK_API_VERSION_MINOR(picked->properties().apiVersion) << '.'
              << VK_API_VERSION_PATCH(picked->properties().apiVersion) << '\n';

    while (!window->should_close()) {
        engine.begin_frame();
        window->poll_events();
        // Render work lands in feat/swapchain-clear.
        engine.end_frame();
    }

    (void)engine.shutdown();
    return EXIT_SUCCESS;
}
