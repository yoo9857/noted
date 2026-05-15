// Application entry. Wires Engine + Window + GPU stack and renders a
// textured fullscreen quad. The texture is loaded from
// <executable-dir>/sample.png if present, otherwise a procedural 256x256
// magenta-on-grey checkerboard so the demo runs without external assets.

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
#include <vector>

#include "noted/engine/engine.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/allocator.hpp"
#include "noted/engine/gpu/descriptor_pool.hpp"
#include "noted/engine/gpu/descriptor_set.hpp"
#include "noted/engine/gpu/descriptor_set_layout.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/image.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/gpu/renderer.hpp"
#include "noted/engine/gpu/sampler.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/gpu/surface.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/gpu/upload.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/platform/fs/fs.hpp"
#include "noted/platform/image_io/image_io.hpp"
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
    (void)reg.on_pointer_pressed.subscribe([](const noted::hook::PointerPressed& p) {
        std::cout << "pointer down " << p.x << ", " << p.y
                  << " btn=" << static_cast<int>(p.button) << '\n';
    });
    (void)reg.on_framebuffer_resized.subscribe([](const noted::hook::FramebufferResized& r) {
        std::cout << "framebuffer resized to " << r.width << "x" << r.height << '\n';
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
    const noted::gpu::Device&     device,
    const std::filesystem::path&  path) -> noted::Result<noted::gpu::ShaderModule> {
    auto spv = noted::platform::fs::read_spirv(path);
    if (!spv) {
        return std::unexpected(std::move(spv).error());
    }
    return noted::gpu::ShaderModule::create(device, *spv);
}

// 256x256 magenta-on-grey checkerboard, RGBA8.
[[nodiscard]] auto make_checkerboard() -> noted::platform::image_io::LoadedImage {
    constexpr std::uint32_t kSize = 256;
    constexpr std::uint32_t kTile = 32;
    noted::platform::image_io::LoadedImage out;
    out.width  = kSize;
    out.height = kSize;
    out.pixels.resize(static_cast<std::size_t>(kSize) * kSize * 4U);

    auto byte = [](int v) { return static_cast<std::byte>(v); };
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            const bool checker = ((x / kTile) ^ (y / kTile)) & 1U;
            const auto idx = (y * kSize + x) * 4U;
            if (checker) {
                out.pixels[idx + 0] = byte(0xC8);  // magenta-ish
                out.pixels[idx + 1] = byte(0x40);
                out.pixels[idx + 2] = byte(0xA0);
            } else {
                out.pixels[idx + 0] = byte(0x20);  // dark grey
                out.pixels[idx + 1] = byte(0x20);
                out.pixels[idx + 2] = byte(0x24);
            }
            out.pixels[idx + 3] = byte(0xFF);
        }
    }
    return out;
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

    auto allocator = noted::gpu::Allocator::create(*instance, *physical, *device);
    if (!allocator) {
        std::cerr << allocator.error().format() << '\n';
        device->wait_idle();
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

    // -------- Texture: try sample.png next to the binary, else checkerboard.
    auto loaded = noted::platform::image_io::load_rgba8("sample.png");
    if (!loaded) {
        std::cout << "sample.png not loaded (" << loaded.error().message
                  << "), using procedural checkerboard\n";
        loaded = make_checkerboard();
    }

    auto texture = noted::gpu::Image::create(*allocator, noted::gpu::ImageCreateInfo{
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {loaded->width, loaded->height, 1U},
        .usage  = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    });
    if (!texture) {
        std::cerr << texture.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    if (auto r = noted::gpu::upload_image_pixels(*allocator, *device, *texture,
            loaded->pixels, noted::gpu::UploadImageInfo{
                .queue        = device->graphics_queue(),
                .queue_family = device->graphics_family(),
            }); !r) {
        std::cerr << r.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto sampler = noted::gpu::Sampler::linear_clamp(*device);
    if (!sampler) {
        std::cerr << sampler.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    // -------- Descriptor set with combined image sampler at (set=0, binding=0).
    const std::array<noted::gpu::DescriptorBinding, 1> bindings{{{
        .binding = 0,
        .type    = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count   = 1,
        .stages  = VK_SHADER_STAGE_FRAGMENT_BIT,
    }}};
    auto set_layout = noted::gpu::DescriptorSetLayout::create(*device, bindings);
    if (!set_layout) {
        std::cerr << set_layout.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    const std::array<noted::gpu::DescriptorPoolSize, 1> pool_sizes{{{
        .type  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
    }}};
    auto pool = noted::gpu::DescriptorPool::create(*device,
        noted::gpu::DescriptorPoolCreateInfo{
            .max_sets   = 1,
            .pool_sizes = pool_sizes,
        });
    if (!pool) {
        std::cerr << pool.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    auto raw_set = pool->allocate(*set_layout);
    if (!raw_set) {
        std::cerr << raw_set.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }
    noted::gpu::DescriptorSet descriptor_set{device->handle(), *raw_set};
    noted::gpu::DescriptorWriter{descriptor_set}
        .write_combined_image_sampler(0, texture->view(), sampler->handle())
        .commit();

    // -------- Pipeline: textured quad.
    const std::filesystem::path shader_dir{NOTED_SHADER_DIR};
    auto vert = load_shader(*device, shader_dir / "fullscreen.vs_main.spv");
    auto frag = load_shader(*device, shader_dir / "fullscreen.ps_textured.spv");
    if (!vert || !frag) {
        std::cerr << (!vert ? vert.error().format() : frag.error().format()) << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    const std::array<const noted::gpu::DescriptorSetLayout*, 1> layouts{
        &*set_layout,
    };
    auto pipeline_layout = noted::gpu::PipelineLayout::create(*device, layouts);
    if (!pipeline_layout) {
        std::cerr << pipeline_layout.error().format() << '\n';
        device->wait_idle();
        (void)engine.shutdown();
        return EXIT_FAILURE;
    }

    // Slang entry-point names survive into the SPIR-V module; pass them
    // explicitly so vkCreateGraphicsPipelines finds the right function.
    auto pipeline = noted::gpu::GraphicsPipelineBuilder{}
        .add_stage(VK_SHADER_STAGE_VERTEX_BIT,   *vert, "vs_main")
        .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *frag, "ps_textured")
        .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
                       VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .color_format(swapchain->summary().color_format)
        .build(*device, *pipeline_layout);
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

    const VkPipeline        pipeline_h = pipeline->handle();
    const VkPipelineLayout  layout_h   = pipeline_layout->handle();
    const VkDescriptorSet   set_h      = descriptor_set.handle();
    noted::gpu::Renderer::DrawCallback draw =
        [pipeline_h, layout_h, set_h](VkCommandBuffer cb, VkExtent2D /*ext*/) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_h);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    layout_h, /*firstSet=*/0,
                                    /*setCount=*/1, &set_h,
                                    /*dynamicOffsetCount=*/0, nullptr);
            vkCmdDraw(cb, /*vertexCount=*/3, /*instanceCount=*/1, 0, 0);
        };

    while (!window->should_close()) {
        engine.begin_frame();
        window->poll_events();

        VkClearColorValue clear{};
        clear.float32[0] = 0.05F;
        clear.float32[1] = 0.05F;
        clear.float32[2] = 0.10F;
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
