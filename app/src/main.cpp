// Application entry. Wires Engine + Window + GPU stack and runs the frame
// loop. Renders a fullscreen triangle each frame to prove the pipeline
// path end-to-end (shader load → pipeline build → draw → present).

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>

#include "noted/engine/engine.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/gpu/renderer.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/gpu/surface.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/platform/fs/fs.hpp"
#include "noted/platform/window/window.hpp"

#ifndef NOTED_SHADER_DIR
#  define NOTED_SHADER_DIR "shaders"
#endif

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
            std::string{"glfwCreateWindowSurface failed: VkResult="} +
                std::to_string(static_cast<int>(vr))));
    }
    return raw;
}

[[nodiscard]] auto load_shader(
    const noted::gpu::Device& device,
    const std::filesystem::path& path) -> noted::Result<noted::gpu::ShaderModule> {
    auto spv = noted::platform::fs::read_spirv(path);
    if (!spv) {
        return std::unexpected(std::move(spv).error());
    }
    return noted::gpu::ShaderModule::create(device, *spv);
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

    // Shaders + pipeline.
    const std::filesystem::path shader_dir{NOTED_SHADER_DIR};
    auto vert = load_shader(*device, shader_dir / "fullscreen_triangle.vert.spv");
    if (!vert) {
        std::cerr << vert.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }
    auto frag = load_shader(*device, shader_dir / "fullscreen_triangle.frag.spv");
    if (!frag) {
        std::cerr << frag.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto layout = noted::gpu::PipelineLayout::create(*device, {}, {});
    if (!layout) {
        std::cerr << layout.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto pipeline = noted::gpu::GraphicsPipelineBuilder{}
        .add_stage(VK_SHADER_STAGE_VERTEX_BIT,   *vert)
        .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *frag)
        .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                       VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .color_format(swapchain->summary().color_format)
        .build(*device, *layout);
    if (!pipeline) {
        std::cerr << pipeline.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto renderer = noted::gpu::Renderer::create(*device, *swapchain);
    if (!renderer) {
        std::cerr << renderer.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto recreate_swapchain = [&]() -> noted::Result<void> {
        device->wait_idle();
        const auto [w, h] = window->framebuffer_size();
        if (w == 0 || h == 0) {
            return {};
        }
        if (auto r = swapchain->recreate(*physical, surface, VkExtent2D{w, h},
                noted::gpu::SwapchainConfig{
                    .desired_image_count = 3,
                    .prefer_srgb         = true,
                    .allow_mailbox       = !static_cast<bool>(flag_force_vsync),
                    .force_fifo          = static_cast<bool>(flag_force_vsync),
                }); !r) {
            return std::unexpected(std::move(r).error());
        }
        return renderer->rebind_swapchain(*device, *swapchain);
    };

    const VkPipeline pipeline_h = pipeline->handle();
    noted::gpu::Renderer::DrawCallback draw =
        [pipeline_h](VkCommandBuffer cb, VkExtent2D /*extent*/) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_h);
            vkCmdDraw(cb, /*vertexCount=*/3, /*instanceCount=*/1,
                      /*firstVertex=*/0, /*firstInstance=*/0);
        };

    while (!window->should_close()) {
        engine.begin_frame();
        window->poll_events();

        const auto t = static_cast<float>(engine.frame_index()) * 0.003F;
        VkClearColorValue clear{};
        clear.float32[0] = 0.05F + 0.05F * std::sin(t);
        clear.float32[1] = 0.05F + 0.05F * std::sin(t + 2.094F);
        clear.float32[2] = 0.10F + 0.05F * std::sin(t + 4.188F);
        clear.float32[3] = 1.0F;

        auto rr = renderer->render_frame_with(*device, *swapchain, clear, draw);
        if (!rr) {
            const auto code = rr.error().code;
            if (code == noted::ErrorCode::gpu_swapchain_out_of_date ||
                code == noted::ErrorCode::gpu_swapchain_suboptimal) {
                if (auto recr = recreate_swapchain(); !recr) {
                    std::cerr << recr.error().format() << '\n';
                    break;
                }
            } else {
                std::cerr << rr.error().format() << '\n';
                break;
            }
        }

        engine.end_frame();
    }

    device->wait_idle();
    (void)engine.shutdown();
    return EXIT_SUCCESS;
}
