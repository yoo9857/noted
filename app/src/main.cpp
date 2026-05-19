// Application entry. Wires Engine + Window + GPU stack and renders a
// `domain::LayerGraph` via `compositor::LayerCompositor` into the
// canvas, then composites the canvas onto the swapchain with an
// ImGui overlay on top. The stroke engine still writes pen-input
// stamps over the layer composite, so ink works against the
// real product pipeline now.
//
// The textured-quad demo this file used to host is gone; the LayerGraph
// constructed below exercises every fixed-function blend mode the
// compositor implements (normal / screen / linear_dodge / multiply).

#define GLFW_INCLUDE_VULKAN
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "noted/compositor/layer_compositor.hpp"
#include "noted/compositor/layer_payload.hpp"
#include "noted/domain/command/commands.hpp"
#include "noted/domain/command/undo_stack.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/domain/layer/layer.hpp"
#include "noted/engine/engine.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/allocator.hpp"
#include "noted/engine/gpu/canvas_render_target.hpp"
#include "noted/engine/gpu/descriptor_pool.hpp"
#include "noted/engine/gpu/descriptor_set.hpp"
#include "noted/engine/gpu/descriptor_set_layout.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/instance.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/gpu/renderer.hpp"
#include "noted/engine/gpu/sampler.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/gpu/surface.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"
#include "noted/engine/stroke/stroke_engine.hpp"
#include "noted/platform/fs/fs.hpp"
#include "noted/platform/io/file_picker.hpp"
#include "noted/platform/io/noted_file.hpp"
#include "noted/platform/window/window.hpp"
#include "noted/ui/imgui_host.hpp"
#include "noted/ui/widget/layer_panel.hpp"
#include "noted/ui/widget/menu_bar.hpp"
#include "noted/ui/widget/outline_panel.hpp"
#include "noted/ui/widget/status_bar.hpp"

#ifndef NOTED_SHADER_DIR
#define NOTED_SHADER_DIR "shaders"
#endif

namespace {

noted::harness::FeatureFlag flag_validation_layers{"gpu.enable_validation_layers",
                                                   /*default=*/true};
noted::harness::FeatureFlag flag_force_vsync{"gpu.force_vsync_fifo", /*default=*/false};

// Single source of truth for the CPU-side frames-in-flight count. Passed
// to both the Renderer and the LayerCompositor — the compositor needs the
// matching value to size its descriptor-set rotation (see ADR 0028).
constexpr std::uint32_t kFramesInFlight = 2;

[[nodiscard]] auto glfw_required_extensions() -> std::span<const char* const> {
    std::uint32_t count = 0;
    const char** ptr = glfwGetRequiredInstanceExtensions(&count);
    return {ptr, static_cast<std::size_t>(count)};
}

void install_default_observers() {
    auto& reg = noted::hook::registry();
    (void) reg.on_error.subscribe([](const noted::hook::ErrorObserved& e) {
        std::cerr << "[error] " << e.error.format() << '\n';
    });
    (void) reg.on_frame_end.subscribe([](const noted::hook::FrameEnd& f) {
        if ((f.frame_index % 240) == 0) {
            std::cout << "frame " << f.frame_index << " | cpu " << f.cpu_ms << " ms\n";
        }
    });
    (void) reg.on_pointer_pressed.subscribe([](const noted::hook::PointerPressed& p) {
        std::cout << "pointer down " << p.x << ", " << p.y << " btn=" << static_cast<int>(p.button)
                  << '\n';
    });
    (void) reg.on_framebuffer_resized.subscribe([](const noted::hook::FramebufferResized& r) {
        std::cout << "framebuffer resized to " << r.width << "x" << r.height << '\n';
    });
}

// Probe the OS for a CJK-capable TrueType font, returning the first
// one that exists. Returns an empty path when none of the candidates
// are present — ImGuiHost then keeps the default ProggyClean bitmap
// font (Latin-only, hangul renders as boxes).
[[nodiscard]] auto probe_cjk_font() -> std::filesystem::path {
    static const std::array<const char*, 6> kCandidates{
#if defined(_WIN32)
        // Malgun Gothic ships with every Windows since Vista. The
        // bold variant is the secondary fallback for the rare slim
        // image where the regular face was uninstalled.
        "C:/Windows/Fonts/malgun.ttf",
        "C:/Windows/Fonts/malgunbd.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/Library/Fonts/AppleGothic.ttf",
#else
        // Debian/Ubuntu via fonts-noto-cjk; Arch via noto-fonts-cjk.
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
#endif
        nullptr,
        nullptr,
    };
    std::error_code ec;
    for (const auto* p : kCandidates) {
        if (p == nullptr) {
            continue;
        }
        std::filesystem::path candidate{p};
        if (std::filesystem::exists(candidate, ec) && !ec) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] auto create_window_surface(VkInstance instance, noted::platform::Window& window)
    -> noted::Result<VkSurfaceKHR> {
    VkSurfaceKHR raw = VK_NULL_HANDLE;
    if (auto vr = glfwCreateWindowSurface(instance, window.native_handle(), nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_surface_lost,
                              std::string{"glfwCreateWindowSurface failed: VkResult="} +
                                  std::to_string(static_cast<int>(vr))));
    }
    return raw;
}

[[nodiscard]] auto load_shader(const noted::gpu::Device& device, const std::filesystem::path& path)
    -> noted::Result<noted::gpu::ShaderModule> {
    auto spv = noted::platform::fs::read_spirv(path);
    if (!spv) {
        return std::unexpected(std::move(spv).error());
    }
    return noted::gpu::ShaderModule::create(device, *spv);
}

// Build a small demo LayerGraph + payload store that exercises every
// fixed-function blend mode the compositor implements. Four layers
// stacked in a deterministic order via explicit input edges so the
// topological walk lands the same way on every run:
//
//     bg (normal)        — dark navy base
//      └── red (normal)  — semi-transparent red over bg
//           └── add (linear_dodge)  — additive cool blue glow
//                └── warm (multiply) — warm-tone tint over everything
//
// Result on screen: dark navy → muted red → blue glow → warm overlay.
// Replacing this with a `Document` loaded from disk is the next step;
// for v0.x scaffold the in-source demo proves the pipeline is wired.
struct DemoScene {
    noted::domain::LayerGraph graph;
    noted::compositor::LayerPayloadStore store;
};

[[nodiscard]] auto build_demo_scene() -> noted::Result<DemoScene> {
    using BM = noted::domain::BlendMode;
    using LK = noted::domain::LayerKind;
    DemoScene s;

    const auto bg = s.graph.add_layer(LK::bitmap, "background");
    const auto red = s.graph.add_layer(LK::bitmap, "red");
    const auto add = s.graph.add_layer(LK::bitmap, "additive glow");
    const auto warm = s.graph.add_layer(LK::bitmap, "warm tint");

    if (auto r = s.graph.set_blend(red, BM::normal); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_opacity(red, 0.60F); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_blend(add, BM::linear_dodge); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_opacity(add, 0.50F); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_blend(warm, BM::multiply); !r) {
        return std::unexpected(std::move(r).error());
    }

    // Chain the topology so the topological walk produces bg → red → add → warm.
    if (auto r = s.graph.set_inputs(red, std::array{bg}); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_inputs(add, std::array{red}); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = s.graph.set_inputs(warm, std::array{add}); !r) {
        return std::unexpected(std::move(r).error());
    }

    s.store.set(bg, noted::compositor::SolidColor{0.15F, 0.18F, 0.22F, 1.0F});
    s.store.set(red, noted::compositor::SolidColor{0.85F, 0.25F, 0.30F, 1.0F});
    s.store.set(add, noted::compositor::SolidColor{0.20F, 0.45F, 0.95F, 1.0F});
    s.store.set(warm, noted::compositor::SolidColor{0.95F, 0.85F, 0.70F, 1.0F});

    return s;
}

}  // namespace

int main() {
    NOTED_PROFILE_THREAD("main");
    install_default_observers();

    noted::engine::Engine engine;
    if (auto r = engine.init(); !r) {
        std::cerr << r.error().format() << '\n';
        return EXIT_FAILURE;
    }

    auto window = noted::platform::Window::create({
        .title = "noted",
        .width = 1600,
        .height = 1000,
    });
    if (!window) {
        std::cerr << window.error().format() << '\n';
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto instance = noted::gpu::Instance::create({
        .app_name = "noted",
        .app_version = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .api_version = VK_API_VERSION_1_3,
        .enable_validation = static_cast<bool>(flag_validation_layers),
        .extra_extensions = {},
        .surface_extensions = glfw_required_extensions(),
    });
    if (!instance) {
        std::cerr << instance.error().format() << '\n';
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto raw_surface = create_window_surface(instance->handle(), *window);
    if (!raw_surface) {
        std::cerr << raw_surface.error().format() << '\n';
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }
    auto surface = noted::gpu::Surface::adopt(*instance, *raw_surface);

    auto physical = noted::gpu::PhysicalDevice::select(*instance);
    if (!physical) {
        std::cerr << physical.error().format() << '\n';
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }
    std::cout << "selected GPU: " << physical->properties().deviceName << '\n';

    auto device = noted::gpu::Device::create(*physical, surface);
    if (!device) {
        std::cerr << device.error().format() << '\n';
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto allocator = noted::gpu::Allocator::create(*instance, *physical, *device);
    if (!allocator) {
        std::cerr << allocator.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    const auto [fb_w, fb_h] = window->framebuffer_size();
    auto swapchain =
        noted::gpu::Swapchain::create(*physical,
                                      *device,
                                      surface,
                                      VkExtent2D{fb_w, fb_h},
                                      noted::gpu::SwapchainConfig{
                                          .desired_image_count = 3,
                                          .prefer_srgb = true,
                                          .allow_mailbox = !static_cast<bool>(flag_force_vsync),
                                          .force_fifo = static_cast<bool>(flag_force_vsync),
                                      });
    if (!swapchain) {
        std::cerr << swapchain.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto sampler = noted::gpu::Sampler::linear_clamp(*device);
    if (!sampler) {
        std::cerr << sampler.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    // -------- Descriptor layout for the composite pass that samples
    // the canvas onto the swapchain. The compositor and stroke engine
    // create their own descriptor sets internally, so this layout is
    // only the canvas → swapchain blit's combined image sampler at
    // (set=0, binding=0).
    const std::array<noted::gpu::DescriptorBinding, 1> bindings{{{
        .binding = 0,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
        .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
    }}};
    auto set_layout = noted::gpu::DescriptorSetLayout::create(*device, bindings);
    if (!set_layout) {
        std::cerr << set_layout.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    // One descriptor set — the canvas sampler used by the composite
    // pass. The canvas set is rewritten on every swapchain recreate
    // because canvas.view() changes.
    const std::array<noted::gpu::DescriptorPoolSize, 1> pool_sizes{{{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
    }}};
    auto pool = noted::gpu::DescriptorPool::create(*device,
                                                   noted::gpu::DescriptorPoolCreateInfo{
                                                       .max_sets = 1,
                                                       .pool_sizes = pool_sizes,
                                                   });
    if (!pool) {
        std::cerr << pool.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto raw_canvas_set = pool->allocate(*set_layout);
    if (!raw_canvas_set) {
        std::cerr << raw_canvas_set.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }
    noted::gpu::DescriptorSet canvas_set{device->handle(), *raw_canvas_set};

    // -------- Canvas render target (offscreen image both passes share).
    constexpr VkFormat kCanvasFormat = VK_FORMAT_R8G8B8A8_UNORM;
    auto canvas = noted::gpu::CanvasRenderTarget::create(*allocator,
                                                         noted::gpu::CanvasCreateInfo{
                                                             .extent = swapchain->summary().extent,
                                                             .format = kCanvasFormat,
                                                         });
    if (!canvas) {
        std::cerr << canvas.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }
    noted::gpu::DescriptorWriter{canvas_set}
        .write_combined_image_sampler(0, canvas->view(), sampler->handle())
        .commit();

    // -------- Composite pipeline: samples the canvas into the swapchain.
    // Only one pipeline now (previously two — the to-canvas one was the
    // textured-quad demo; LayerCompositor replaces it below).
    const std::filesystem::path shader_dir{NOTED_SHADER_DIR};
    auto vert = load_shader(*device, shader_dir / "fullscreen.vs_main.spv");
    auto frag = load_shader(*device, shader_dir / "fullscreen.ps_textured.spv");
    if (!vert || !frag) {
        std::cerr << (!vert ? vert.error().format() : frag.error().format()) << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    const std::array<const noted::gpu::DescriptorSetLayout*, 1> layouts{
        &*set_layout,
    };
    auto pipeline_layout = noted::gpu::PipelineLayout::create(*device, layouts);
    if (!pipeline_layout) {
        std::cerr << pipeline_layout.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto pipeline_to_swapchain =
        noted::gpu::GraphicsPipelineBuilder{}
            .add_stage(VK_SHADER_STAGE_VERTEX_BIT, *vert, "main")
            .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *frag, "main")
            .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .color_format(swapchain->summary().color_format)
            .build(*device, *pipeline_layout);
    if (!pipeline_to_swapchain) {
        std::cerr << pipeline_to_swapchain.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    // -------- Layer compositor: fills the canvas by walking a LayerGraph
    // in topological order. Replaces the textured-quad-to-canvas pipeline
    // that lived here before. Per ADR 0019, the compositor owns its own
    // pipeline objects (one per fixed-function blend mode) + descriptor
    // pool + sampler + 1×1 "all selected" dummy mask.
    auto layer_vs = load_shader(*device, shader_dir / "layer.vs_layer.spv");
    auto layer_ps = load_shader(*device, shader_dir / "layer.ps_layer.spv");
    if (!layer_vs || !layer_ps) {
        std::cerr << (!layer_vs ? layer_vs.error().format() : layer_ps.error().format()) << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto layer_compositor = noted::compositor::LayerCompositor::create({
        .allocator = &*allocator,
        .device = &*device,
        .vs_module = &*layer_vs,
        .ps_module = &*layer_ps,
        .canvas_format = kCanvasFormat,
        .graphics_queue = device->graphics_queue(),
        .graphics_family = device->graphics_family(),
        .frames_in_flight = kFramesInFlight,
    });
    if (!layer_compositor) {
        std::cerr << layer_compositor.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto scene = build_demo_scene();
    if (!scene) {
        std::cerr << scene.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    // -------- Stroke engine: ink drawn into the canvas on top of the
    // layer composite. Subscribes to pointer + framebuffer-resize hooks.
    auto stamp_vs = load_shader(*device, shader_dir / "stamp.vs_stamp.spv");
    auto stamp_ps = load_shader(*device, shader_dir / "stamp.ps_stamp.spv");
    if (!stamp_vs || !stamp_ps) {
        std::cerr << (!stamp_vs ? stamp_vs.error().format() : stamp_ps.error().format()) << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto stroke_engine = noted::stroke::StrokeEngine::create({
        .device = &*device,
        .vs_module = &*stamp_vs,
        .ps_module = &*stamp_ps,
        .canvas_format = kCanvasFormat,
        .hook_registry = &noted::hook::registry(),
    });
    if (!stroke_engine) {
        std::cerr << stroke_engine.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }
    (*stroke_engine)->set_canvas_size(swapchain->summary().extent);

    auto renderer = noted::gpu::Renderer::create(*device, *swapchain, kFramesInFlight);
    if (!renderer) {
        std::cerr << renderer.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    // -------- Dear ImGui host. Renders an overlay inside the swapchain
    // pass; the existing textured-quad demo still draws underneath
    // until feat/ui-compositor-wire takes over the canvas pass (ADR
    // 0027 follow-up #18b).
    const auto cjk_font_path = probe_cjk_font();
    if (cjk_font_path.empty()) {
        std::cerr << "[font] no CJK font detected — Korean glyphs will render as boxes "
                     "(install fonts-noto-cjk on Linux, ship with Malgun Gothic on Windows)\n";
    } else {
        std::cout << "[font] using CJK font: " << cjk_font_path.string() << '\n';
    }
    auto imgui_host = noted::ui::ImGuiHost::create({
        .instance = &*instance,
        .physical_device = &*physical,
        .device = &*device,
        .window = window->native_handle(),
        .color_format = swapchain->summary().color_format,
        .image_count = swapchain->summary().image_count,
        .cjk_font_path = cjk_font_path,
        .font_size_px = 16.0F,
    });
    if (!imgui_host) {
        std::cerr << imgui_host.error().format() << '\n';
        device->wait_idle();
        (void) engine.shutdown();
        return EXIT_FAILURE;
    }

    auto recreate_swapchain = [&]() -> noted::Result<void> {
        device->wait_idle();
        const auto [w, h] = window->framebuffer_size();
        if (w == 0 || h == 0) {
            return {};
        }
        if (auto r = swapchain->recreate(*physical,
                                         surface,
                                         VkExtent2D{w, h},
                                         noted::gpu::SwapchainConfig{
                                             .desired_image_count = 3,
                                             .prefer_srgb = true,
                                             .allow_mailbox = !static_cast<bool>(flag_force_vsync),
                                             .force_fifo = static_cast<bool>(flag_force_vsync),
                                         });
            !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = renderer->rebind_swapchain(*device, *swapchain); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = canvas->resize(*allocator, swapchain->summary().extent); !r) {
            return std::unexpected(std::move(r).error());
        }
        // canvas->view() is now a fresh handle — re-point the descriptor.
        noted::gpu::DescriptorWriter{canvas_set}
            .write_combined_image_sampler(0, canvas->view(), sampler->handle())
            .commit();
        return {};
    };

    const VkPipeline composite_pipeline_h = pipeline_to_swapchain->handle();
    const VkPipelineLayout layout_h = pipeline_layout->handle();
    const VkDescriptorSet canvas_set_h = canvas_set.handle();
    auto* const stroke_h = stroke_engine->get();
    auto* const compositor_ptr = &*layer_compositor;
    // Mutable refs so the layer panel can toggle visibility / blend on
    // the same graph the compositor walks each frame.
    auto& scene_graph = scene->graph;
    auto& scene_store = scene->store;

    // -------- Document + UndoStack — the live data model the
    // outline panel renders and the Edit menu mutates via commands.
    // Starts empty; the user populates via Edit → Add Block.
    noted::domain::Document document{};
    noted::domain::UndoStack undo_stack{};
    noted::domain::BlockId selected_block = noted::domain::invalid_block_id;

    // File menu state: the on-disk backing path (nullopt for an
    // untitled document) and the undo-stack depth captured at the
    // last successful save. Comparing the current depth against the
    // saved one drives the dirty marker in the title bar — close
    // enough for v0.x; a true dirty bit would diff Document state.
    std::optional<std::filesystem::path> current_path{};
    std::size_t saved_undo_size = 0;

    auto window_title_for = [&]() -> std::string {
        const std::string name =
            current_path.has_value() ? current_path->filename().string() : std::string{"Untitled"};
        const bool dirty = undo_stack.undo_size() != saved_undo_size;
        return std::string{"noted — "} + name + (dirty ? " *" : "");
    };
    glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());

    auto is_dirty = [&]() -> bool { return undo_stack.undo_size() != saved_undo_size; };

    // Discrete actions the "save changes?" modal is gating. Stored across
    // frames because the modal stays open until the user picks one of
    // Save / Discard / Cancel.
    enum class PendingExitAction { none, quit, new_doc };
    PendingExitAction pending_action = PendingExitAction::none;
    // Set when any callsite has armed the prompt; drained at the top of
    // the ImGui frame via `OpenPopup`, which must be called from inside
    // a frame. Decouples "arm from anywhere" from "open in the right
    // ImGui phase."
    bool should_open_dirty_prompt = false;
    // Set ONLY by the modal's Save (on success) or Discard branches.
    // The loop-top close intercept treats this as "the user has
    // already answered" and lets the exit proceed even on a still-
    // dirty doc — guards against the double-X-click race where the
    // second click would otherwise bypass the modal.
    bool confirmed_exit = false;

    auto reset_to_blank_document = [&]() {
        document = noted::domain::Document{};
        undo_stack.clear();
        selected_block = noted::domain::invalid_block_id;
        current_path.reset();
        saved_undo_size = undo_stack.undo_size();
        glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());
    };

    // Save the document to `path`, refreshing the dirty baseline + title
    // on success. Centralized so Save and Save As share the post-write
    // bookkeeping.
    auto write_document_to = [&](const std::filesystem::path& path) -> noted::Result<void> {
        if (auto r = noted::platform::io::save_noted_file(path, document); !r) {
            return std::unexpected(std::move(r).error());
        }
        current_path = path;
        saved_undo_size = undo_stack.undo_size();
        glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());
        return {};
    };

    // Carry out whatever `pending_action` is set to, then clear it.
    // Called by the dirty-prompt modal after Save (on success) or
    // Discard. Cancel does NOT call this — it just clears
    // `pending_action`.
    auto execute_pending_action = [&]() {
        switch (pending_action) {
            case PendingExitAction::quit:
                confirmed_exit = true;
                glfwSetWindowShouldClose(window->native_handle(), GLFW_TRUE);
                break;
            case PendingExitAction::new_doc:
                reset_to_blank_document();
                break;
            case PendingExitAction::none:
                break;
        }
        pending_action = PendingExitAction::none;
    };

    // Save the current document for the dirty-prompt's "Save" button.
    // Mirrors menu_bar's Save semantics: in-place when a backing path
    // exists, else falls through to Save As. Returns true iff a write
    // actually completed — the modal uses this to decide whether to
    // close + execute the pending action or stay open so the user can
    // try a different choice.
    auto save_for_dirty_prompt = [&]() -> bool {
        if (current_path.has_value()) {
            if (auto r = write_document_to(*current_path); !r) {
                std::cerr << r.error().format() << '\n';
                return false;
            }
            return true;
        }
        auto picked = noted::platform::io::pick_noted_save("untitled.noted");
        if (!picked) {
            std::cerr << picked.error().format() << '\n';
            return false;
        }
        if (!picked->has_value()) {
            return false;  // user cancelled the Save As dialog
        }
        std::filesystem::path target = **picked;
        if (target.extension() != ".noted") {
            target += ".noted";
        }
        if (auto r = write_document_to(target); !r) {
            std::cerr << r.error().format() << '\n';
            return false;
        }
        return true;
    };

    noted::ui::widget::MenuBarState menu_state{};

    // The canvas pass:
    //   1) `LayerCompositor` walks the demo LayerGraph in topological
    //      order and fills the canvas (one draw per visible layer
    //      with the right fixed-function blend state).
    //   2) The stroke engine overlays accumulated ink stamps on top.
    // Both happen inside one vkCmdBeginRendering — pipeline switches
    // are cheap relative to a full pass barrier.
    noted::gpu::Renderer::DrawCallback canvas_draw =
        [compositor_ptr, &scene_graph, &scene_store, stroke_h](VkCommandBuffer cb, VkExtent2D ext) {
            compositor_ptr->composite(cb, ext, scene_graph, scene_store);
            stroke_h->record(cb, ext);
        };
    auto* imgui_host_ptr = &*imgui_host;
    noted::gpu::Renderer::DrawCallback composite_draw =
        [composite_pipeline_h, layout_h, canvas_set_h, imgui_host_ptr](VkCommandBuffer cb,
                                                                       VkExtent2D /*ext*/) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, composite_pipeline_h);
            vkCmdBindDescriptorSets(cb,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    layout_h,
                                    /*firstSet=*/0,
                                    /*setCount=*/1,
                                    &canvas_set_h,
                                    /*dynamicOffsetCount=*/0,
                                    nullptr);
            vkCmdDraw(cb, /*vertexCount=*/3, /*instanceCount=*/1, 0, 0);
            // ImGui draws on top of the composited canvas. The
            // surrounding vkCmdBeginRendering (owned by the renderer)
            // is the right context for ImGui_ImplVulkan_RenderDrawData.
            // finalize_frame() was already called above the loop body;
            // here we just record the cached draw data.
            imgui_host_ptr->render_into(cb);
        };

    while (true) {
        // Loop-top close intercept — catches the GLFW window X button,
        // the File → Quit menu item (which calls
        // glfwSetWindowShouldClose internally), and the Ctrl+Q chord
        // (which routes through the same menu signal). Three outcomes:
        //   1. User already answered Save/Discard (`confirmed_exit`)
        //      or the doc is clean → break out of the loop.
        //   2. Dirty, no pending modal yet → arm the prompt and
        //      swallow the GLFW close flag.
        //   3. Dirty, modal already armed for quit (e.g. user double-
        //      clicked X) → swallow the GLFW close flag and let the
        //      existing modal stay visible. Without this, the second
        //      click would slip past and lose the user's edits.
        if (window->should_close()) {
            if (confirmed_exit || !is_dirty()) {
                break;
            }
            glfwSetWindowShouldClose(window->native_handle(), GLFW_FALSE);
            if (pending_action != PendingExitAction::quit) {
                pending_action = PendingExitAction::quit;
                should_open_dirty_prompt = true;
            }
        }

        engine.begin_frame();
        {
            NOTED_PROFILE_ZONE_N("poll_events");
            window->poll_events();
        }

        // ImGui frame setup happens BEFORE renderer.render_with_canvas
        // so ImGui::* calls below land in the same frame the renderer
        // will draw. finalize_frame is called unconditionally below
        // so a swapchain-out-of-date error doesn't leave the frame
        // dangling. render_into runs inside the composite_draw
        // lambda — only when the pass actually executes.
        imgui_host->begin_frame();

        // -------- Product shell (v0.x):
        //   - main menu bar with File / Edit / View / About
        //   - layer panel observing + mutating the scene graph
        //   - status bar pinned to the bottom of the viewport
        //   - ImGui demo window behind a View toggle (off by default)
        const noted::ui::widget::MenuBarStatus menu_status{
            .can_undo = undo_stack.can_undo(),
            .can_redo = undo_stack.can_redo(),
            .has_document_path = current_path.has_value(),
        };
        auto menu = noted::ui::widget::menu_bar(menu_state, menu_status);

        // Keyboard shortcuts — wire the chords the menu's hint column
        // advertises. `IsKeyChordPressed` defaults to RouteGlobal so
        // the chord fires app-wide; once we add text-input widgets
        // that need to consume Ctrl+Z themselves, switch the relevant
        // chord to `ImGuiInputFlags_RouteFocused`. Match menu_bar's
        // semantics exactly: Save falls through to Save As when there
        // is no backing path; Undo / Redo respect `can_undo` /
        // `can_redo` so an empty stack doesn't print error noise.
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) {
            menu.file_new_requested = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
            menu.file_open_requested = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) {
            menu.file_save_as_requested = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            if (menu_status.has_document_path) {
                menu.file_save_requested = true;
            } else {
                menu.file_save_as_requested = true;
            }
        }
        if (menu_status.can_undo && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            menu.undo_requested = true;
        }
        if (menu_status.can_redo && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
            menu.redo_requested = true;
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
            menu.quit_requested = true;
        }

        if (menu.quit_requested) {
            // Window has no `request_close()` wrapper yet — the GLFW
            // pattern is documented enough that going through the
            // native handle is acceptable for now. A small follow-up
            // PR can add the wrapper if a second caller appears.
            // The loop-top intercept above handles the dirty-prompt
            // hand-off; pressing Quit on a clean doc still exits
            // immediately on the next iteration.
            glfwSetWindowShouldClose(window->native_handle(), GLFW_TRUE);
        }
        if (menu.file_new_requested) {
            if (is_dirty()) {
                pending_action = PendingExitAction::new_doc;
                should_open_dirty_prompt = true;
            } else {
                reset_to_blank_document();
            }
        }
        if (menu.file_open_requested) {
            auto picked = noted::platform::io::pick_noted_open();
            if (!picked) {
                std::cerr << picked.error().format() << '\n';
            } else if (picked->has_value()) {
                auto loaded = noted::platform::io::load_noted_file(**picked);
                if (!loaded) {
                    std::cerr << loaded.error().format() << '\n';
                } else {
                    document = std::move(*loaded);
                    undo_stack.clear();
                    selected_block = noted::domain::invalid_block_id;
                    current_path = **picked;
                    saved_undo_size = undo_stack.undo_size();
                    glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());
                }
            }
        }
        if (menu.file_save_requested) {
            if (current_path.has_value()) {
                if (auto r = write_document_to(*current_path); !r) {
                    std::cerr << r.error().format() << '\n';
                }
            }
        }
        if (menu.file_save_as_requested) {
            const std::string default_name =
                current_path.has_value() ? current_path->filename().string() : "untitled.noted";
            auto picked = noted::platform::io::pick_noted_save(default_name);
            if (!picked) {
                std::cerr << picked.error().format() << '\n';
            } else if (picked->has_value()) {
                // NFD does not always append the filter extension on
                // every platform (Windows does, GTK historically does
                // not). Force `.noted` so the file round-trips back
                // through the same filter regardless of OS.
                std::filesystem::path target = **picked;
                if (target.extension() != ".noted") {
                    target += ".noted";
                }
                if (auto r = write_document_to(target); !r) {
                    std::cerr << r.error().format() << '\n';
                }
            }
        }
        if (menu.undo_requested) {
            if (auto r = undo_stack.undo(document); !r) {
                std::cerr << r.error().format() << '\n';
            } else {
                glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());
            }
        }
        if (menu.redo_requested) {
            if (auto r = undo_stack.redo(document); !r) {
                std::cerr << r.error().format() << '\n';
            } else {
                glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());
            }
        }
        if (menu.add_block_requested) {
            // Choose the parent: selected block if it's a group,
            // else the document root, else invalid_block_id which
            // makes AddBlockCommand allocate a fresh root.
            noted::domain::BlockId parent = noted::domain::invalid_block_id;
            if (selected_block != noted::domain::invalid_block_id) {
                if (const auto* node = document.find(selected_block);
                    node != nullptr && node->kind == noted::domain::BlockKind::group) {
                    parent = selected_block;
                } else if (document.root() != noted::domain::invalid_block_id) {
                    parent = document.root();
                }
            } else if (document.root() != noted::domain::invalid_block_id) {
                parent = document.root();
            }
            auto cmd = std::make_unique<noted::domain::AddBlockCommand>(
                *menu.add_block_requested, parent, std::string{});
            auto* cmd_ptr = cmd.get();
            if (auto r = undo_stack.execute(std::move(cmd), document); !r) {
                std::cerr << r.error().format() << '\n';
            } else {
                selected_block = cmd_ptr->assigned_id();
                glfwSetWindowTitle(window->native_handle(), window_title_for().c_str());
            }
        }

        // Drain the queued OpenPopup. ImGui requires the call to come
        // from inside a frame, so the arming sites only set a flag and
        // the actual `OpenPopup` happens here. Reset before opening so
        // the same flag can be re-armed within the same frame without
        // a double-open (BeginPopupModal silently absorbs a second
        // open on an already-open popup, but the explicit reset keeps
        // the state machine readable).
        if (should_open_dirty_prompt) {
            should_open_dirty_prompt = false;
            ImGui::OpenPopup("Unsaved changes##dirty");
        }
        {
            // Center the modal on the main viewport. SetNextWindowPos
            // only takes effect on Appearing — re-runs of the popup
            // pick up wherever the user last moved it, which is the
            // expected ImGui pattern.
            const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        }
        if (ImGui::BeginPopupModal(
                "Unsaved changes##dirty",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted("You have unsaved changes. Save before continuing?");
            ImGui::Spacing();
            constexpr ImVec2 kButton{110.0F, 0.0F};
            if (ImGui::Button("Save", kButton)) {
                if (save_for_dirty_prompt()) {
                    ImGui::CloseCurrentPopup();
                    execute_pending_action();
                }
                // On save failure / user-cancelled Save As: stay in the
                // modal so the user can pick Discard or Cancel.
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", kButton)) {
                ImGui::CloseCurrentPopup();
                execute_pending_action();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", kButton)) {
                pending_action = PendingExitAction::none;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        noted::ui::widget::layer_panel(scene_graph, &menu_state.show_layer_panel);
        noted::ui::widget::outline_panel(document, selected_block, &menu_state.show_outline_panel);
        if (menu_state.show_demo_window) {
            ImGui::ShowDemoWindow(&menu_state.show_demo_window);
        }
        if (menu_state.show_about_window) {
            ImGui::SetNextWindowSize({340.0F, 0.0F}, ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    "About noted", &menu_state.show_about_window, ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextUnformatted("noted — note-taking + raster editor");
                ImGui::TextUnformatted("노트 + 래스터 이미지 에디터");
                ImGui::TextUnformatted("v0.x development build");
                ImGui::Spacing();
                ImGui::TextDisabled("Engine: C++23 + Vulkan 1.4 + Slang");
                ImGui::TextDisabled("UI: Dear ImGui (ADR 0027)");
            }
            ImGui::End();
        }
        noted::ui::widget::status_bar({.frame_index = engine.frame_index()});

        imgui_host->finalize_frame();

        // Canvas pass clears to opaque black — the source texture will
        // overdraw the whole surface, but defending against pipeline-state
        // surprises is cheap.
        VkClearColorValue canvas_clear{};
        canvas_clear.float32[0] = 0.0F;
        canvas_clear.float32[1] = 0.0F;
        canvas_clear.float32[2] = 0.0F;
        canvas_clear.float32[3] = 1.0F;

        // Swapchain pass clears to the previous demo's dark teal — what
        // shows through if the composite quad ever leaves edges blank
        // (it doesn't today, but it's a useful regression tell).
        VkClearColorValue swapchain_clear{};
        swapchain_clear.float32[0] = 0.05F;
        swapchain_clear.float32[1] = 0.05F;
        swapchain_clear.float32[2] = 0.10F;
        swapchain_clear.float32[3] = 1.0F;

        auto rr = renderer->render_with_canvas(
            *device,
            *swapchain,
            *canvas,
            noted::gpu::Renderer::CanvasPassDesc{canvas_clear, canvas_draw},
            noted::gpu::Renderer::SwapchainPassDesc{swapchain_clear, composite_draw});
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
        NOTED_PROFILE_FRAME();
    }

    device->wait_idle();
    (void) engine.shutdown();
    return EXIT_SUCCESS;
}
