#include "app.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <imgui.h>
#include <vulkan/vulkan.h>

#include "noted/compositor/layer_payload.hpp"
#include "noted/domain/command/commands.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/domain/io/brush_library_json.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"
#include "noted/platform/fs/fs.hpp"
#include "noted/platform/image_io/image_io.hpp"
#include "noted/platform/io/file_picker.hpp"
#include "noted/platform/io/noted_file.hpp"

#include "io/font_probe.hpp"

namespace noted::app {

namespace {

// Frames-in-flight default lives on AppConfig now (Phase-1 config
// migration, ADR 0030). The Renderer + LayerCompositor still
// receive the value via direct parameters during init; the config
// snapshot in `App::cfg_.canvas.frames_in_flight` is the single
// runtime source.

noted::harness::FeatureFlag flag_validation_layers{"gpu.enable_validation_layers",
                                                   /*default=*/true};
noted::harness::FeatureFlag flag_force_vsync{"gpu.force_vsync_fifo", /*default=*/false};

// Surface extensions our VkInstance needs so QVulkanInstance can
// build a VkSurfaceKHR for the QWindow. The list is fixed per
// platform; previously this came from glfwGetRequiredInstanceExtensions
// but Qt's bridge does not expose an equivalent for the
// "use-this-existing-VkInstance" path, so we hardcode it.
[[nodiscard]] auto required_surface_extensions() -> std::span<const char* const> {
#if defined(_WIN32)
    static constexpr std::array<const char*, 2> kExts{"VK_KHR_surface", "VK_KHR_win32_surface"};
#elif defined(__APPLE__)
    static constexpr std::array<const char*, 3> kExts{
        "VK_KHR_surface", "VK_EXT_metal_surface", "VK_KHR_portability_enumeration"};
#else
    static constexpr std::array<const char*, 3> kExts{
        "VK_KHR_surface", "VK_KHR_xcb_surface", "VK_KHR_wayland_surface"};
#endif
    return {kExts.data(), kExts.size()};
}

// Monotonic time-since-startup in seconds. Replaces glfwGetTime.
[[nodiscard]] auto monotonic_seconds() noexcept -> double {
    static const auto epoch = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count();
}

// Surface creation moved into `Window::create_surface` (ADR 0034
// phase 1) — the Window now owns the QVulkanInstance bridge that
// generates the VkSurfaceKHR, so App stops carrying any
// platform-specific surface logic.

[[nodiscard]] auto load_shader(const noted::gpu::Device& device, const std::filesystem::path& path)
    -> noted::Result<noted::gpu::ShaderModule> {
    auto spv = noted::platform::fs::read_spirv(path);
    if (!spv) {
        return std::unexpected(std::move(spv).error());
    }
    return noted::gpu::ShaderModule::create(device, *spv);
}

constexpr VkFormat kCanvasFormat = VK_FORMAT_R8G8B8A8_UNORM;

}  // namespace

// ---- create / destroy ------------------------------------------------------

auto App::create(config::AppConfig cfg) -> Result<std::unique_ptr<App>> {
    auto app = std::unique_ptr<App>(new App{});
    app->cfg_ = std::move(cfg);
    if (auto r = app->init_engine_and_window(); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = app->init_gpu_stack(); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = app->init_canvas_pipeline(); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = app->init_layer_compositor(); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = app->init_page_renderer(); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = app->init_stroke_engine(); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = app->init_renderer_and_imgui(); !r) {
        return std::unexpected(std::move(r).error());
    }
    // Brush library: seed factory presets, then layer the user-added
    // presets from disk on top. Failures load best-effort — a
    // corrupt or missing `brushes.json` should never block the app,
    // just degrade to factory-only.
    app->brush_library_ = noted::domain::tool::BrushLibrary::with_builtins();
    app->load_user_brush_library();

    // Build the render passes + UI panels coordinators AFTER all
    // GPU resources are alive. The `Deps` structs snapshot non-
    // owning references; App outlives both so the references stay
    // valid for the entire process lifetime.
    app->render_passes_ =
        noted::app::frame::RenderPasses::create(noted::app::frame::RenderPasses::Deps{
            .device = *app->device_,
            .swapchain = *app->swapchain_,
            .renderer = *app->renderer_,
            .canvas = *app->canvas_,
            .strokes_target = *app->strokes_target_,
            .page_renderer = *app->page_renderer_,
            .session = app->session_,
            .layer_compositor = *app->layer_compositor_,
            .scene = *app->scene_,
            .stroke_engine = *app->stroke_engine_,
            .composite_pipeline_layout = *app->composite_pipeline_layout_,
            .composite_pipeline = *app->composite_pipeline_,
            .composite_overlay_pipeline = *app->composite_overlay_pipeline_,
            .canvas_set = app->canvas_set_,
            .strokes_set = app->strokes_set_,
            .camera = app->camera_,
            .imgui_host = *app->imgui_host_,
        });

    // Set the initial title from a clean session before the first
    // paint — avoids the "noted" → "noted — Untitled" flash on
    // frame 0.
    app->last_window_title_ = app->session_.title();
    app->window_->set_title(app->last_window_title_);

    // Apply the configured default theme exactly once before the
    // first frame so the very first paint is already styled.
    app->applied_theme_ = app->cfg_.ui.default_theme;
    app->menu_state_.theme = app->cfg_.ui.default_theme;
    noted::ui::theme::apply(app->applied_theme_);

    // Seed the page-strip toggle from config — user can still flip
    // it at runtime via View → Page strip.
    app->menu_state_.show_page_strip = app->cfg_.ui.show_page_strip;

    // Seed the camera's canvas + window extents before the first
    // frame. The `framebuffer_resized` hook only fires on subsequent
    // resizes — without this the very first render uses Camera's
    // 1×1 defaults and the canvas collapses to a single pixel.
    //
    // Initial pages have already been seeded by `init_page_renderer`,
    // so call `ensure_canvas_fits_pages` to size the offscreen target
    // to the page stack before the first paint. The camera's canvas
    // extent is updated inside that call; window extent stays at the
    // swapchain extent.
    const auto sc_extent = app->swapchain_->summary().extent;
    app->camera_.set_window_extent(sc_extent.width, sc_extent.height);
    if (auto r = app->ensure_canvas_fits_pages(); !r) {
        return std::unexpected(std::move(r).error());
    }
    // Centre the camera on the first page (or on the canvas centre
    // if the document has no pages). Without this seed the canvas
    // appears at the top-left of the window on first launch instead
    // of the natural Goodnotes / Photoshop "open with the document
    // centred" UX. Translation is in screen pixels:
    //   screen = canvas * scale + translation
    // We want the page-list centre's `canvas` position to land at
    // the window centre on screen.
    {
        const auto& pages = app->session_.document().pages().pages();
        double target_cx = app->camera_.canvas_extent_w() * 0.5;
        double target_cy = app->camera_.canvas_extent_h() * 0.5;
        if (!pages.empty()) {
            const auto& p0 = pages.front();
            target_cx = static_cast<double>(p0.origin_x_px) + p0.extent_w_px * 0.5;
            target_cy = static_cast<double>(p0.origin_y_px) + p0.extent_h_px * 0.5;
        }
        const double tx =
            static_cast<double>(sc_extent.width) * 0.5 - target_cx * app->camera_.scale();
        const double ty =
            static_cast<double>(sc_extent.height) * 0.5 - target_cy * app->camera_.scale();
        app->camera_.set_translation(tx, ty);
    }

    app->install_frame_hook();

    // UiPanels is built LAST — it depends on `tool_input_router_`
    // and `selection_handler_` which `install_frame_hook` creates.
    // Deps struct includes two `std::function` callbacks that bind
    // App methods so the dirty-prompt's save flow stays on App
    // (it touches the GLFW window handle and the session's path).
    auto* raw = app.get();
    app->ui_panels_ = noted::app::ui::UiPanels::create(noted::app::ui::UiPanels::Deps{
        .engine = raw->engine_,
        .session = raw->session_,
        .scene = *raw->scene_,
        .layer_compositor = *raw->layer_compositor_,
        .stroke_engine = *raw->stroke_engine_,
        .tool_input_router = *raw->tool_input_router_,
        .selection_handler = raw->selection_handler_,
        .shape_handler = raw->shape_handler_,
        .text_handler = raw->text_handler_,
        .image_handler = raw->image_handler_,
        .camera = raw->camera_,
        .swapchain = *raw->swapchain_,
        .cfg = raw->cfg_,
        .menu_state = raw->menu_state_,
        .debug_overlay_state = raw->debug_overlay_state_,
        .outline_rename = raw->outline_rename_,
        .tools = raw->tools_,
        .brush_library = raw->brush_library_,
        .active_brush_preset = &raw->active_brush_preset_,
        .on_apply_preset =
            [raw](noted::domain::tool::BrushPresetId id) {
                if (const auto* p = raw->brush_library_.find(id); p != nullptr) {
                    noted::domain::tool::apply_preset_to(*p, raw->tools_.pen);
                    raw->active_brush_preset_ = id;
                    raw->pen_at_last_apply_ = raw->tools_.pen;
                }
            },
        .on_save_preset =
            [raw](std::string suggested_name) {
                noted::domain::tool::BrushPreset p{};
                // Build a uniqueness-safe default name by appending
                // "(N)" if the suggested name collides.
                std::string name =
                    suggested_name.empty() ? std::string{"My brush"} : std::move(suggested_name);
                int suffix = 1;
                std::string candidate = name;
                while (raw->brush_library_.find_by_name(candidate) != nullptr) {
                    ++suffix;
                    candidate = name + " (" + std::to_string(suffix) + ")";
                }
                p.name = candidate;
                p.kind = noted::domain::tool::BrushKind::pen;
                p.min_radius_px = raw->tools_.pen.min_radius_px;
                p.max_radius_px = raw->tools_.pen.max_radius_px;
                p.alpha_gamma = raw->tools_.pen.alpha_gamma;
                p.pressure_curve = raw->tools_.pen.pressure_curve;
                p.r = raw->tools_.pen.r;
                p.g = raw->tools_.pen.g;
                p.b = raw->tools_.pen.b;
                p.a = raw->tools_.pen.a;
                p.stabilizer = raw->tools_.pen.stabilizer;
                p.use_preset_color = true;
                auto id = raw->brush_library_.add(std::move(p));
                if (id) {
                    raw->active_brush_preset_ = *id;
                    raw->pen_at_last_apply_ = raw->tools_.pen;
                    raw->save_user_brush_library();
                } else {
                    std::cerr << id.error().format() << '\n';
                }
            },
        .on_remove_preset =
            [raw](noted::domain::tool::BrushPresetId id) {
                if (raw->brush_library_.is_builtin(id)) {
                    return;  // factory presets can't be removed
                }
                if (auto r = raw->brush_library_.remove(id); !r) {
                    std::cerr << r.error().format() << '\n';
                    return;
                }
                if (raw->active_brush_preset_ == id) {
                    raw->active_brush_preset_ = noted::domain::tool::invalid_brush_preset_id;
                }
                raw->save_user_brush_library();
            },
        .selection = raw->selection_,
        .shapes = raw->session_.document().shapes(),
        .texts = raw->session_.document().texts(),
        .images = raw->session_.document().images(),
        .prompt = raw->prompt_,
        .save_for_dirty_prompt = [raw]() -> bool { return raw->save_for_dirty_prompt(); },
        .execute_pending_dirty_action =
            [raw](DirtyPrompt::PendingAction a) { raw->execute_pending_dirty_action(a); },
        .on_pick_image = [raw]() { raw->run_image_picker(); },
    });

    return app;
}

App::~App() {
    if (device_.has_value()) {
        device_->wait_idle();
    }
    (void) engine_.shutdown();
}

// ---- Init steps ------------------------------------------------------------

auto App::init_engine_and_window() -> noted::Result<void> {
    if (auto r = engine_.init(); !r) {
        return std::unexpected(std::move(r).error());
    }
    auto window = noted::platform::Window::create({
        .title = cfg_.window.title,
        .width = cfg_.window.width,
        .height = cfg_.window.height,
    });
    if (!window) {
        return std::unexpected(std::move(window).error());
    }
    window_.emplace(std::move(*window));
    return {};
}

auto App::init_gpu_stack() -> noted::Result<void> {
    auto instance = noted::gpu::Instance::create({
        .app_name = "noted",
        .app_version = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .api_version = VK_API_VERSION_1_3,
        .enable_validation = static_cast<bool>(flag_validation_layers),
        .extra_extensions = {},
        .surface_extensions = required_surface_extensions(),
    });
    if (!instance) {
        return std::unexpected(std::move(instance).error());
    }
    instance_.emplace(std::move(*instance));

    auto raw_surface = window_->create_surface(instance_->handle());
    if (!raw_surface) {
        return std::unexpected(std::move(raw_surface).error());
    }
    surface_ = noted::gpu::Surface::adopt(*instance_, *raw_surface);

    auto physical = noted::gpu::PhysicalDevice::select(*instance_);
    if (!physical) {
        return std::unexpected(std::move(physical).error());
    }
    physical_.emplace(std::move(*physical));
    std::cout << "selected GPU: " << physical_->properties().deviceName << '\n';

    auto device = noted::gpu::Device::create(*physical_, *surface_);
    if (!device) {
        return std::unexpected(std::move(device).error());
    }
    device_.emplace(std::move(*device));

    auto allocator = noted::gpu::Allocator::create(*instance_, *physical_, *device_);
    if (!allocator) {
        return std::unexpected(std::move(allocator).error());
    }
    allocator_.emplace(std::move(*allocator));

    const auto [fb_w, fb_h] = window_->framebuffer_size();
    auto swapchain =
        noted::gpu::Swapchain::create(*physical_,
                                      *device_,
                                      *surface_,
                                      VkExtent2D{fb_w, fb_h},
                                      noted::gpu::SwapchainConfig{
                                          .desired_image_count = 3,
                                          .prefer_srgb = true,
                                          .allow_mailbox = !static_cast<bool>(flag_force_vsync),
                                          .force_fifo = static_cast<bool>(flag_force_vsync),
                                      });
    if (!swapchain) {
        return std::unexpected(std::move(swapchain).error());
    }
    swapchain_.emplace(std::move(*swapchain));
    return {};
}

auto App::init_canvas_pipeline() -> noted::Result<void> {
    auto sampler = noted::gpu::Sampler::linear_clamp(*device_);
    if (!sampler) {
        return std::unexpected(std::move(sampler).error());
    }
    sampler_.emplace(std::move(*sampler));

    // Composite pass samples the canvas onto the swapchain. One
    // descriptor binding (the canvas combined image sampler).
    const std::array<noted::gpu::DescriptorBinding, 1> bindings{{{
        .binding = 0,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
        .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
    }}};
    auto set_layout = noted::gpu::DescriptorSetLayout::create(*device_, bindings);
    if (!set_layout) {
        return std::unexpected(std::move(set_layout).error());
    }
    composite_set_layout_.emplace(std::move(*set_layout));

    // Two descriptor sets: one for the canvas (sampled by the swapchain
    // composite), one for the strokes target (sampled by the canvas
    // overlay). Same layout, same sampler, different image view.
    const std::array<noted::gpu::DescriptorPoolSize, 1> pool_sizes{{{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 2,
    }}};
    auto pool = noted::gpu::DescriptorPool::create(*device_,
                                                   noted::gpu::DescriptorPoolCreateInfo{
                                                       .max_sets = 2,
                                                       .pool_sizes = pool_sizes,
                                                   });
    if (!pool) {
        return std::unexpected(std::move(pool).error());
    }
    composite_descriptor_pool_.emplace(std::move(*pool));

    auto raw_canvas_set = composite_descriptor_pool_->allocate(*composite_set_layout_);
    if (!raw_canvas_set) {
        return std::unexpected(std::move(raw_canvas_set).error());
    }
    canvas_set_ = noted::gpu::DescriptorSet{device_->handle(), *raw_canvas_set};

    auto raw_strokes_set = composite_descriptor_pool_->allocate(*composite_set_layout_);
    if (!raw_strokes_set) {
        return std::unexpected(std::move(raw_strokes_set).error());
    }
    strokes_set_ = noted::gpu::DescriptorSet{device_->handle(), *raw_strokes_set};

    auto canvas = noted::gpu::CanvasRenderTarget::create(*allocator_,
                                                         noted::gpu::CanvasCreateInfo{
                                                             .extent = swapchain_->summary().extent,
                                                             .format = kCanvasFormat,
                                                         });
    if (!canvas) {
        return std::unexpected(std::move(canvas).error());
    }
    canvas_.emplace(std::move(*canvas));

    auto strokes = noted::gpu::StrokeTarget::create(*allocator_,
                                                    noted::gpu::StrokeTargetCreateInfo{
                                                        .extent = swapchain_->summary().extent,
                                                        .format = kCanvasFormat,
                                                    });
    if (!strokes) {
        return std::unexpected(std::move(strokes).error());
    }
    strokes_target_.emplace(std::move(*strokes));

    noted::gpu::DescriptorWriter{canvas_set_}
        .write_combined_image_sampler(0, canvas_->view(), sampler_->handle())
        .commit();
    noted::gpu::DescriptorWriter{strokes_set_}
        .write_combined_image_sampler(0, strokes_target_->view(), sampler_->handle())
        .commit();

    // Composite pipeline (canvas → swapchain).
    const auto shader_dir = config::resolve_shader_dir(cfg_);
    if (shader_dir.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::file_not_found,
                              "App: shader directory not found. Set NOTED_SHADER_DIR, place "
                              "`shaders/` next to the executable, or override via "
                              "noted.config.json `assets.shaderDir`."));
    }
    auto vert = load_shader(*device_, shader_dir / "fullscreen.vs_main.spv");
    if (!vert) {
        return std::unexpected(std::move(vert).error());
    }
    fullscreen_vs_.emplace(std::move(*vert));
    auto frag = load_shader(*device_, shader_dir / "fullscreen.ps_textured.spv");
    if (!frag) {
        return std::unexpected(std::move(frag).error());
    }
    fullscreen_ps_.emplace(std::move(*frag));

    // Composite pass now takes a 16-byte vertex-stage push constant
    // carrying the Camera's NDC-space scale + translation. See
    // `shaders/fullscreen.slang` for the matching `CompositePush`.
    VkPushConstantRange composite_push{};
    composite_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    composite_push.offset = 0;
    composite_push.size = sizeof(float) * 4;  // float2 scale + float2 translation

    const std::array<const noted::gpu::DescriptorSetLayout*, 1> layouts{&*composite_set_layout_};
    auto pipeline_layout = noted::gpu::PipelineLayout::create(
        *device_, layouts, std::span<const VkPushConstantRange>{&composite_push, 1});
    if (!pipeline_layout) {
        return std::unexpected(std::move(pipeline_layout).error());
    }
    composite_pipeline_layout_.emplace(std::move(*pipeline_layout));

    auto pipeline =
        noted::gpu::GraphicsPipelineBuilder{}
            .add_stage(VK_SHADER_STAGE_VERTEX_BIT, *fullscreen_vs_, "main")
            .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *fullscreen_ps_, "main")
            .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .color_format(swapchain_->summary().color_format)
            .build(*device_, *composite_pipeline_layout_);
    if (!pipeline) {
        return std::unexpected(std::move(pipeline).error());
    }
    composite_pipeline_.emplace(std::move(*pipeline));

    // Overlay pipeline — same shader, same layout, but different target
    // format (canvas, not swapchain) and SRC_OVER blend so the
    // strokes_target alpha controls how much of its colour reaches the
    // canvas. Where strokes alpha=0 (no ink or fully erased), the canvas
    // pixel is unchanged and the page pattern shows through.
    VkPipelineColorBlendAttachmentState overlay_blend{};
    overlay_blend.blendEnable = VK_TRUE;
    overlay_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    overlay_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    overlay_blend.colorBlendOp = VK_BLEND_OP_ADD;
    overlay_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    overlay_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    overlay_blend.alphaBlendOp = VK_BLEND_OP_ADD;
    overlay_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    auto overlay_pipeline =
        noted::gpu::GraphicsPipelineBuilder{}
            .add_stage(VK_SHADER_STAGE_VERTEX_BIT, *fullscreen_vs_, "main")
            .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *fullscreen_ps_, "main")
            .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .color_blend_attachment(overlay_blend)
            .color_format(kCanvasFormat)
            .build(*device_, *composite_pipeline_layout_);
    if (!overlay_pipeline) {
        return std::unexpected(std::move(overlay_pipeline).error());
    }
    composite_overlay_pipeline_.emplace(std::move(*overlay_pipeline));
    return {};
}

auto App::init_layer_compositor() -> noted::Result<void> {
    const auto shader_dir = config::resolve_shader_dir(cfg_);
    if (shader_dir.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::file_not_found,
                              "App: shader directory not found. Set NOTED_SHADER_DIR, place "
                              "`shaders/` next to the executable, or override via "
                              "noted.config.json `assets.shaderDir`."));
    }
    auto layer_vs = load_shader(*device_, shader_dir / "layer.vs_layer.spv");
    if (!layer_vs) {
        return std::unexpected(std::move(layer_vs).error());
    }
    layer_vs_.emplace(std::move(*layer_vs));
    auto layer_ps = load_shader(*device_, shader_dir / "layer.ps_layer.spv");
    if (!layer_ps) {
        return std::unexpected(std::move(layer_ps).error());
    }
    layer_ps_.emplace(std::move(*layer_ps));

    auto layer_compositor = noted::compositor::LayerCompositor::create({
        .allocator = &*allocator_,
        .device = &*device_,
        .vs_module = &*layer_vs_,
        .ps_module = &*layer_ps_,
        .canvas_format = kCanvasFormat,
        .graphics_queue = device_->graphics_queue(),
        .graphics_family = device_->graphics_family(),
        .frames_in_flight = cfg_.canvas.frames_in_flight,
    });
    if (!layer_compositor) {
        return std::unexpected(std::move(layer_compositor).error());
    }
    layer_compositor_.emplace(std::move(*layer_compositor));

    auto scene = build_demo_scene();
    if (!scene) {
        return std::unexpected(std::move(scene).error());
    }
    scene_.emplace(std::move(*scene));
    return {};
}

auto App::init_page_renderer() -> noted::Result<void> {
    const auto shader_dir = config::resolve_shader_dir(cfg_);
    if (shader_dir.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::file_not_found,
                              "App: shader directory not found. Set NOTED_SHADER_DIR, place "
                              "`shaders/` next to the executable, or override via "
                              "noted.config.json `assets.shaderDir`."));
    }
    auto vs = load_shader(*device_, shader_dir / "page_bg.vs_page_bg.spv");
    if (!vs) {
        return std::unexpected(std::move(vs).error());
    }
    page_bg_vs_.emplace(std::move(*vs));
    auto ps = load_shader(*device_, shader_dir / "page_bg.ps_page_bg.spv");
    if (!ps) {
        return std::unexpected(std::move(ps).error());
    }
    page_bg_ps_.emplace(std::move(*ps));

    auto pr = noted::canvas::PageRenderer::create({
        .device = &*device_,
        .vs_module = &*page_bg_vs_,
        .ps_module = &*page_bg_ps_,
        .canvas_format = kCanvasFormat,
    });
    if (!pr) {
        return std::unexpected(std::move(pr).error());
    }
    page_renderer_.emplace(std::move(*pr));

    // Seed with three demo pages — mixed backgrounds so the page
    // strip has something distinct to show on first run, all
    // horizontally centred at the identity camera transform. Page
    // dimensions + initial backgrounds come from AppConfig so a
    // future Preferences UI can flip them without recompiling.
    //
    // Seed is written **directly** into the document, NOT through
    // AddPageCommand — these aren't user edits, so they shouldn't
    // sit in the undo stack and they shouldn't mark the document as
    // dirty. (`is_dirty()` compares undo size to saved baseline; both
    // stay zero with this path.)
    const auto canvas = swapchain_->summary().extent;
    const float page_w = cfg_.canvas.default_page_extent_w_px;
    const float page_h = cfg_.canvas.default_page_extent_h_px;
    const float origin_x = std::max(20.0F, (static_cast<float>(canvas.width) - page_w) * 0.5F);
    constexpr std::array<noted::canvas::PageBackground, 3> kDemoBackgrounds{{
        noted::canvas::PageBackground::grid,
        noted::canvas::PageBackground::lined,
        noted::canvas::PageBackground::dotted,
    }};
    for (const auto bg : kDemoBackgrounds) {
        (void) session_.document().add_page(page_w, page_h, bg, origin_x);
    }
    // Seed a default canvas layer so the Layers panel is populated on
    // first launch — without it the user sees an empty placeholder
    // until their first stroke, which reads as "the panel isn't
    // working". Same direct-write rationale as the page seed above:
    // not a user edit, so the undo stack stays clean and the dirty
    // marker stays off until the user actually changes something.
    if (session_.document().canvas_layers().empty()) {
        (void) session_.document().add_canvas_layer("Layer 1");
    }
    return {};
}

auto App::init_stroke_engine() -> noted::Result<void> {
    const auto shader_dir = config::resolve_shader_dir(cfg_);
    if (shader_dir.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::file_not_found,
                              "App: shader directory not found. Set NOTED_SHADER_DIR, place "
                              "`shaders/` next to the executable, or override via "
                              "noted.config.json `assets.shaderDir`."));
    }
    auto stamp_vs = load_shader(*device_, shader_dir / "polyline.vs_polyline.spv");
    if (!stamp_vs) {
        return std::unexpected(std::move(stamp_vs).error());
    }
    stamp_vs_.emplace(std::move(*stamp_vs));
    auto stamp_ps = load_shader(*device_, shader_dir / "polyline.ps_polyline.spv");
    if (!stamp_ps) {
        return std::unexpected(std::move(stamp_ps).error());
    }
    stamp_ps_.emplace(std::move(*stamp_ps));

    auto stroke = noted::stroke::StrokeEngine::create({
        .allocator = &*allocator_,
        .device = &*device_,
        .vs_module = &*stamp_vs_,
        .ps_module = &*stamp_ps_,
        .canvas_format = kCanvasFormat,
        .hook_registry = &noted::hook::registry(),
    });
    if (!stroke) {
        return std::unexpected(std::move(stroke).error());
    }
    stroke_engine_ = std::move(*stroke);
    stroke_engine_->set_canvas_size(swapchain_->summary().extent);
    // Page-bound the stroke engine: a press only starts a stroke
    // when its canvas-pixel coordinates fall inside one of the
    // document's pages. The desk colour around pages stays
    // un-drawable — matches Goodnotes / Notability convention and
    // makes the page-on-desk visual model (ADR 0033) feel real
    // rather than decorative. The predicate captures `this` by
    // pointer; App outlives the StrokeEngine.
    stroke_engine_->set_press_predicate([this](double canvas_x, double canvas_y) -> bool {
        return session_.document().pages().contains_point(canvas_x, canvas_y);
    });
    // Wire the completion sink so each released stroke executes an
    // AddStrokeCommand against the session's UndoStack. Strokes
    // participate in undo/redo and survive save/load through
    // `.noted` v7 (P.S.4). A failed execute() is non-fatal — the
    // in-flight UX is already done; we surface the error to stderr
    // and continue. The fallback "lost ink" is preferable to
    // crashing mid-session.
    stroke_engine_->set_stroke_sink([this](noted::stroke::Stroke s) {
        auto cmd = std::make_unique<noted::domain::AddStrokeCommand>(std::move(s));
        auto* raw_cmd = cmd.get();
        if (auto r = session_.execute(std::move(cmd)); !r) {
            std::cerr << r.error().format() << '\n';
            return;
        }
        // Notify the smart-shape recognizer so it can attempt
        // detection after the configured hold-still interval. The
        // tick happens in `on_frame`.
        shape_recognizer_.on_stroke_added(raw_cmd->assigned_index(), monotonic_seconds());
    });
    return {};
}

void App::load_user_brush_library() noexcept {
    const auto exe_dir = noted::platform::fs::executable_dir();
    if (exe_dir.empty()) {
        return;
    }
    const auto path = exe_dir / "brushes.json";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return;  // no user library yet — factory-only, fine
    }
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        std::cerr << "[brushes] could not open " << path.string() << '\n';
        return;
    }
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (auto r = noted::domain::io::brush_library_user_from_json(contents, brush_library_); !r) {
        std::cerr << "[brushes] " << r.error().format() << " — falling back to factory-only\n";
    }
}

void App::save_user_brush_library() noexcept {
    const auto exe_dir = noted::platform::fs::executable_dir();
    if (exe_dir.empty()) {
        return;
    }
    const auto path = exe_dir / "brushes.json";
    const auto text = noted::domain::io::brush_library_user_to_json(brush_library_);
    try {
        std::ofstream out{path, std::ios::trunc | std::ios::binary};
        if (!out) {
            std::cerr << "[brushes] could not open " << path.string() << " for write\n";
            return;
        }
        out << text;
    } catch (const std::exception& e) {
        std::cerr << "[brushes] save failed: " << e.what() << '\n';
    }
}

auto App::init_renderer_and_imgui() -> noted::Result<void> {
    auto renderer =
        noted::gpu::Renderer::create(*device_, *swapchain_, cfg_.canvas.frames_in_flight);
    if (!renderer) {
        return std::unexpected(std::move(renderer).error());
    }
    renderer_.emplace(std::move(*renderer));

    // Resolve CJK font path: explicit config override wins; else
    // walk the per-OS candidate list. Empty result → ImGui falls
    // back to ProggyClean (Latin-only) and Hangul renders as boxes
    // — degraded, not fatal.
    std::filesystem::path cjk_font_path = cfg_.font.cjk_font_path;
    std::error_code path_ec;
    if (!cjk_font_path.empty() && (!std::filesystem::exists(cjk_font_path, path_ec) || path_ec)) {
        std::cerr << "[font] config-supplied cjk path missing: " << cjk_font_path.string()
                  << " — falling back to OS probe\n";
        cjk_font_path.clear();
    }
    if (cjk_font_path.empty()) {
        cjk_font_path = probe_cjk_font();
    }
    if (cjk_font_path.empty()) {
        std::cerr << "[font] no CJK font detected — Korean glyphs will render as boxes "
                     "(install fonts-noto-cjk on Linux, ship with Malgun Gothic on Windows)\n";
    } else {
        std::cout << "[font] using CJK font: " << cjk_font_path.string() << '\n';
    }
    auto symbol_font_path = probe_symbol_font();
    if (!symbol_font_path.empty()) {
        std::cout << "[font] using symbol fallback: " << symbol_font_path.string() << '\n';
    }
    auto imgui_host = noted::ui::ImGuiHost::create({
        .instance = &*instance_,
        .physical_device = &*physical_,
        .device = &*device_,
        .color_format = swapchain_->summary().color_format,
        .image_count = swapchain_->summary().image_count,
        .symbol_font_path = symbol_font_path,
        .cjk_font_path = cjk_font_path,
        .font_size_px = cfg_.font.size_px,
    });
    if (!imgui_host) {
        return std::unexpected(std::move(imgui_host).error());
    }
    imgui_host_.emplace(std::move(*imgui_host));
    return {};
}

void App::install_frame_hook() {
    // Push the prior frame's CPU time into the debug overlay's ring
    // buffer. The lambda captures `this`; the App is non-movable +
    // heap-allocated so `this` stays stable for the rest of the
    // process. Subscription survives until shutdown drains the
    // registry — by then no more frames fire.
    (void) noted::hook::registry().on_frame_end.subscribe([this](const noted::hook::FrameEnd& f) {
        debug_overlay_state_.push_sample(static_cast<float>(f.cpu_ms));
    });

    // Camera input — pan + zoom + cursor tracking + framebuffer-
    // resize all live on CameraController after Phase R.2 (ADR 0032).
    // Built BEFORE the tool input router so the camera's subscriptions
    // run first in the channel's insertion order; tools see the
    // camera-side state (cursor, current scale) as already-current
    // when their handlers fire.
    camera_controller_ =
        noted::app::input::CameraController::create(noted::hook::registry(), camera_, cfg_.canvas);

    // Tool input router — owns LEFT-button dispatch to per-tool
    // handlers. Heap-allocated for stable `this` (router's lambdas
    // capture themselves). Middle button is owned by the camera
    // controller above; right button is open for a future context
    // menu.
    tool_input_router_ =
        noted::app::input::ToolInputRouter::create(noted::hook::registry(), camera_);
    auto sel_handler = std::make_unique<noted::app::input::SelectionToolHandler>(selection_);
    selection_handler_ = sel_handler.get();
    tool_input_router_->register_handler(std::move(sel_handler));
    // Phase B.5 + persistence consolidation — Shape tool emits
    // `AddShapeCommand`s via the session's execute path. The lambda
    // captures `this` so undo / redo / dirty all route through the
    // single session, and a future load/save round-trips shapes
    // through `.noted` v3.
    auto shape_handler = std::make_unique<noted::app::input::ShapeToolHandler>(
        [this](std::unique_ptr<noted::domain::Command> cmd) {
            (void) session_.execute(std::move(cmd));
        },
        tools_);
    shape_handler_ = shape_handler.get();
    tool_input_router_->register_handler(std::move(shape_handler));
    // Phase B.6 + persistence consolidation — Text tool emits
    // `AddTextCommand`s via the session's execute path so undo /
    // .noted round-trip work.
    auto text_handler = std::make_unique<noted::app::input::TextToolHandler>(
        [this](std::unique_ptr<noted::domain::Command> cmd) {
            (void) session_.execute(std::move(cmd));
        },
        tools_);
    text_handler_ = text_handler.get();
    tool_input_router_->register_handler(std::move(text_handler));
    // Phase B.7 + persistence consolidation — Image tool emits
    // `AddImageCommand`s via the session's execute path. Real GPU
    // upload (raster body) still pending — see B.7.b.
    auto image_handler = std::make_unique<noted::app::input::ImageToolHandler>(
        [this](std::unique_ptr<noted::domain::Command> cmd) {
            (void) session_.execute(std::move(cmd));
        },
        tools_);
    image_handler_ = image_handler.get();
    tool_input_router_->register_handler(std::move(image_handler));
    tool_input_router_->set_active(tools_.active);

    // Subscribe pointer events to notify the smart-shape recognizer
    // so any pen motion cancels a pending recognition. Without this,
    // a user who released and then started drawing again before the
    // hold-still elapsed could still get their second stroke
    // auto-converted off the back of the first stroke's pending
    // detection. The hook lambdas capture `this`; App outlives the
    // subscriptions.
    (void) noted::hook::registry().on_pointer_pressed.subscribe(
        [this](const noted::hook::PointerPressed&) {
            shape_recognizer_.on_pointer_active(monotonic_seconds());
        });
    (void) noted::hook::registry().on_pointer_moved.subscribe(
        [this](const noted::hook::PointerMoved&) {
            shape_recognizer_.on_pointer_active(monotonic_seconds());
        });

    // Default smart-shape recognizer config — enabled by default so
    // the "draw and hold" gesture works out of the box (Goodnotes
    // convention). A brush_options toggle to switch it off lands in
    // the UI-overhaul slice.
    noted::app::input::ShapeRecognizerConfig rec_cfg{};
    rec_cfg.enabled = true;
    rec_cfg.hold_seconds = 0.6;
    shape_recognizer_.set_config(rec_cfg);
}

// ---- Frame loop -----------------------------------------------------------

auto App::run() -> int {
    while (true) {
        if (should_break_loop()) {
            break;
        }
        on_frame();
    }
    return EXIT_SUCCESS;
}

auto App::should_break_loop() -> bool {
    // Loop-top close intercept — catches the GLFW window X button,
    // the File → Quit menu item (which calls
    // glfwSetWindowShouldClose internally), and the Ctrl+Q chord
    // (which routes through the same menu signal). Three outcomes:
    //   1. User already answered Save/Discard (`prompt_.has_confirmed_exit()`)
    //      or the doc is clean → break out of the loop.
    //   2. Dirty, no pending modal yet → arm the prompt and
    //      swallow the GLFW close flag.
    //   3. Dirty, modal already armed for quit (e.g. user double-
    //      clicked X) → swallow the GLFW close flag and let the
    //      existing modal stay visible. Without this, the second
    //      click would slip past and lose the user's edits.
    if (!window_->should_close()) {
        return false;
    }
    if (prompt_.has_confirmed_exit() || !session_.is_dirty()) {
        return true;
    }
    window_->set_should_close(false);
    if (prompt_.pending_action() != DirtyPrompt::PendingAction::quit) {
        prompt_.arm(DirtyPrompt::PendingAction::quit);
    }
    return false;
}

void App::on_frame() {
    engine_.begin_frame();
    {
        NOTED_PROFILE_ZONE_N("poll_events");
        window_->poll_events();
    }

    // Sync the stroke engine's input-side view transform with the
    // current camera. Cheap (three doubles) so we just do it every
    // frame rather than wiring a change callback through the
    // camera. The stroke engine applies the inverse on each pointer
    // event so stamps land at canvas pixels, not screen pixels.
    stroke_engine_->set_view_transform(
        camera_.translation_x(), camera_.translation_y(), camera_.scale());

    // Smart-shape recognizer tick. Runs detection on the last
    // completed stroke if the configured hold-still interval has
    // elapsed without further pointer activity. On success, swaps
    // the freehand stroke for a recognised ShapePrimitive — two
    // separate commands so the user can Ctrl+Z once to restore
    // the raw stroke if they wanted the freehand.
    if (auto action = shape_recognizer_.tick(session_.document(), monotonic_seconds()); action) {
        if (auto r = session_.execute(
                std::make_unique<noted::domain::RemoveStrokeCommand>(action->stroke_index));
            !r) {
            std::cerr << r.error().format() << '\n';
        } else if (auto r2 = session_.execute(
                       std::make_unique<noted::domain::AddShapeCommand>(action->shape));
                   !r2) {
            std::cerr << r2.error().format() << '\n';
        }
    }

    // ImGui frame setup happens BEFORE renderer.render_with_canvas
    // so ImGui::* calls below land in the same frame the renderer
    // will draw. finalize_frame is called unconditionally so a
    // swapchain-out-of-date error doesn't leave the frame dangling.
    const auto [fb_w, fb_h] = window_->framebuffer_size();
    static double last_frame_time = monotonic_seconds();
    const double now = monotonic_seconds();
    const float dt = static_cast<float>(now - last_frame_time);
    last_frame_time = now;
    imgui_host_->begin_frame(static_cast<float>(fb_w), static_cast<float>(fb_h), dt);

    // Workspace dockspace — must run BEFORE any `ImGui::Begin` so
    // every panel (the App-level Colour / Navigator pair AND the
    // UiPanels-driven Layers / Brush / Outline / Pages) lands in
    // its assigned dock node on first launch. Moving this into
    // `UiPanels::draw()` was a bug — UiPanels runs AFTER the
    // App-level panels each frame, so Colour / Navigator's Begin
    // happened with no dockspace and they floated at their
    // SetNextWindowPos seed instead of docking right.
    noted::ui::widget::workspace_begin(workspace_state_);

    // Mac-style window chrome — drawn FIRST so it sits at the top
    // of the Z-band. Wires the traffic-light buttons into
    // platform::Window's verbs (minimize / toggle_maximize /
    // request_close / start_system_drag). Phase 2 of ADR 0034.
    const auto chrome =
        noted::ui::widget::draw_mac_chrome(last_window_title_, window_->is_maximized());
    if (chrome.close_clicked) {
        window_->request_close();
    }
    if (chrome.minimize_clicked) {
        window_->minimize();
    }
    if (chrome.maximize_clicked) {
        window_->toggle_maximize();
    }
    if (chrome.drag_started) {
        window_->start_system_drag();
    }

    const noted::ui::widget::MenuBarStatus menu_status{
        .can_undo = session_.undo_stack().can_undo(),
        .can_redo = session_.undo_stack().can_redo(),
        .has_document_path = session_.has_path(),
    };
    auto menu = noted::ui::widget::menu_bar(menu_state_, menu_status);
    apply_theme_if_changed();
    wire_keyboard_shortcuts(menu_status, menu);
    handle_menu_actions(menu);

    // 12 o'clock floating toolbar (Phase 3 of ADR 0034). Sits just
    // below the menu bar, hosts the per-tool icon buttons + undo /
    // redo. Replaces the retired left-rail `tool_palette`.
    {
        constexpr float kMenuBarGuessPx = 22.0F;  // ImGui's main menu bar height at default font
        constexpr float kToolbarTopMarginPx = 8.0F;
        const float toolbar_y =
            noted::ui::widget::mac_chrome_height_px() + kMenuBarGuessPx + kToolbarTopMarginPx;
        const noted::ui::widget::TopToolbarStatus toolbar_status{
            .can_undo = menu_status.can_undo,
            .can_redo = menu_status.can_redo,
        };
        const auto tt = noted::ui::widget::top_toolbar(tools_.active, toolbar_status, toolbar_y);
        if (tt.switch_request && *tt.switch_request != tools_.active) {
            tools_.active = *tt.switch_request;
            if (tool_input_router_) {
                tool_input_router_->set_active(tools_.active);
            }
        }
        if (tt.undo_clicked && session_.undo_stack().can_undo()) {
            (void) session_.undo();
        }
        if (tt.redo_clicked && session_.undo_stack().can_redo()) {
            (void) session_.redo();
        }
    }

    // Right-side floating colour picker panel — OpenCanvas /
    // Photoshop-style HSV picker + hex + recent-colours ring. Bound
    // to the active tool's colour fields where they exist (Pen,
    // Shape, Text). Eraser / Select / Image have no stroke colour
    // so the panel is skipped for those tools.
    {
        float* active_rgba = nullptr;
        switch (tools_.active) {
            case noted::domain::tool::ToolKind::pen:
                active_rgba = &tools_.pen.r;
                break;
            case noted::domain::tool::ToolKind::shape:
                active_rgba = &tools_.shape.stroke_r;
                break;
            case noted::domain::tool::ToolKind::text:
                active_rgba = &tools_.text.r;
                break;
            default:
                break;
        }
        if (active_rgba != nullptr) {
            constexpr float kColorPanelTopOffset = 80.0F;
            const auto result = noted::ui::widget::color_picker_panel(
                active_rgba, palette_colors_, kColorPanelTopOffset);

            // Adds `colour` to the palette: dedup head (don't push
            // duplicates), prefer filling empty (alpha=0) slots
            // before evicting populated tail entries.
            const auto push_to_palette = [this](const std::array<float, 4>& colour) {
                if (palette_colors_[0] == colour) {
                    return;
                }
                // First, prefer collapsing into an empty slot if one
                // exists — keeps user-deleted gaps intact rather
                // than evicting populated entries.
                std::size_t evict_idx = palette_colors_.size() - 1;
                for (std::size_t i = 0; i < palette_colors_.size(); ++i) {
                    if (palette_colors_[i][3] <= 0.0F) {
                        evict_idx = i;
                        break;
                    }
                }
                for (std::size_t i = evict_idx; i > 0; --i) {
                    palette_colors_[i] = palette_colors_[i - 1];
                }
                palette_colors_[0] = colour;
            };

            // Palette adds ONLY on explicit user request ('+' button) —
            // never on a committed colour change. The old `committed`
            // path auto-spammed the palette every time the user
            // released a slider or pressed Enter on the hex input,
            // which was confusing and overwrote curated entries.
            if (result.palette_add_requested) {
                push_to_palette({active_rgba[0], active_rgba[1], active_rgba[2], active_rgba[3]});
            }
            if (result.palette_delete_index >= 0 &&
                result.palette_delete_index < static_cast<int>(palette_colors_.size())) {
                palette_colors_[static_cast<std::size_t>(result.palette_delete_index)] = {
                    0.0F, 0.0F, 0.0F, 0.0F};
            }
        }
    }

    // Navigator panel — canvas thumbnail with viewport-rect overlay.
    // Click / drag inside the thumbnail re-centres the camera on the
    // corresponding canvas point. Lives in the same right-side
    // floating column as the colour picker.
    {
        noted::ui::widget::NavigatorInputs nav_in{};
        nav_in.canvas_w = camera_.canvas_extent_w() > 0.0
                              ? static_cast<std::uint32_t>(camera_.canvas_extent_w())
                              : 0U;
        nav_in.canvas_h = camera_.canvas_extent_h() > 0.0
                              ? static_cast<std::uint32_t>(camera_.canvas_extent_h())
                              : 0U;
        nav_in.translation_x = camera_.translation_x();
        nav_in.translation_y = camera_.translation_y();
        nav_in.scale = camera_.scale();
        nav_in.window_w = camera_.window_extent_w() > 0.0
                              ? static_cast<std::uint32_t>(camera_.window_extent_w())
                              : 0U;
        nav_in.window_h = camera_.window_extent_h() > 0.0
                              ? static_cast<std::uint32_t>(camera_.window_extent_h())
                              : 0U;
        constexpr float kNavigatorTopOffset = 420.0F;
        const auto nav = noted::ui::widget::navigator_panel(
            nav_in, session_.document().pages().pages(), kNavigatorTopOffset);
        if (nav.pan_to_canvas_point) {
            const auto [cx, cy] = *nav.pan_to_canvas_point;
            const double window_cx = camera_.window_extent_w() * 0.5;
            const double window_cy = camera_.window_extent_h() * 0.5;
            camera_.set_translation(window_cx - cx * camera_.scale(),
                                    window_cy - cy * camera_.scale());
        }
    }

    draw_widgets();
    refresh_window_title_if_changed();

    imgui_host_->finalize_frame();

    if (auto r = render_one_frame(); !r) {
        std::cerr << r.error().format() << '\n';
        // Render fatal — force loop exit by signalling confirmed
        // exit so should_break_loop returns true regardless of
        // dirty state. Better than crashing with state mid-flight.
        window_->set_should_close(true);
    }

    engine_.end_frame();
    NOTED_PROFILE_FRAME();
}

void App::apply_theme_if_changed() {
    if (menu_state_.theme != applied_theme_) {
        applied_theme_ = menu_state_.theme;
        noted::ui::theme::apply(applied_theme_);
    }
}

void App::wire_keyboard_shortcuts(const noted::ui::widget::MenuBarStatus& status,
                                  noted::ui::widget::MenuBarResult& menu) {
    // Yield every chord to a focused text-input widget. Without
    // this, Ctrl+Z during a block rename would undo the *document*
    // instead of cancelling whatever the user was typing — the
    // exact RouteFocused collision the inline comment in #49 flagged.
    // `IsKeyChordPressed` defaults to RouteGlobal so the check has
    // to happen at the caller side; ImGui has no per-chord
    // RouteFocused that respects InputText capture.
    if (ImGui::GetIO().WantTextInput) {
        return;
    }
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
        if (status.has_document_path) {
            menu.file_save_requested = true;
        } else {
            menu.file_save_as_requested = true;
        }
    }
    if (status.can_undo && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        menu.undo_requested = true;
    }
    if (status.can_redo && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        menu.redo_requested = true;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
        menu.quit_requested = true;
    }

    // Clipboard chords — Ctrl+X (Cut), Ctrl+C (Copy), Ctrl+V (Paste),
    // Delete (delete selection). Shapes only in v1; text / image /
    // stroke clipboard land in follow-up PRs. Each branch is a
    // single domain transition that goes through the UndoStack so
    // the user can Ctrl+Z any of them.
    const auto picks = noted::domain::shapes_in_selection(session_.document(), selection_);
    if (!picks.empty() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
        std::vector<noted::domain::tool::ShapePrimitive> copied;
        copied.reserve(picks.size());
        for (auto i : picks) {
            copied.push_back(session_.document().shapes()[i]);
        }
        clipboard_.set_shapes(std::move(copied));
    }
    if (!picks.empty() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
        std::vector<noted::domain::tool::ShapePrimitive> copied;
        copied.reserve(picks.size());
        for (auto i : picks) {
            copied.push_back(session_.document().shapes()[i]);
        }
        clipboard_.set_shapes(std::move(copied));
        if (auto r = session_.execute(std::make_unique<noted::domain::DeleteShapesCommand>(picks));
            !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (!picks.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        if (auto r = session_.execute(std::make_unique<noted::domain::DeleteShapesCommand>(picks));
            !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (clipboard_.has_shapes() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        // Paste with a small offset so the user sees the new copies
        // sitting just below+right of the source — matches every
        // mainstream editor's UX.
        constexpr double kPasteOffsetPx = 16.0;
        auto pasted = clipboard_.shapes();
        for (auto& s : pasted) {
            s.x0 += kPasteOffsetPx;
            s.y0 += kPasteOffsetPx;
            s.x1 += kPasteOffsetPx;
            s.y1 += kPasteOffsetPx;
        }
        if (auto r = session_.execute(
                std::make_unique<noted::domain::PasteShapesCommand>(std::move(pasted)));
            !r) {
            std::cerr << r.error().format() << '\n';
        }
    }

    // F2 = rename the selected block. Gated on "have a selection",
    // "no rename already in flight", and (already-checked above)
    // "no text input is consuming keys." Pre-fills the buffer from
    // the current name; the outline_panel widget takes it from there.
    if (ImGui::IsKeyPressed(ImGuiKey_F2) &&
        session_.selected_block() != noted::domain::invalid_block_id &&
        outline_rename_.target == noted::domain::invalid_block_id) {
        const auto sel = session_.selected_block();
        if (const auto* node = session_.document().find(sel); node != nullptr) {
            outline_rename_.target = sel;
            outline_rename_.needs_focus = true;
            outline_rename_.commit_requested = false;
            outline_rename_.cancel_requested = false;
            const auto& src = node->name;
            // -1 to leave room for the null terminator.
            const auto cap = outline_rename_.buffer.size() - 1;
            const auto n = std::min(src.size(), cap);
            std::copy_n(src.begin(), n, outline_rename_.buffer.begin());
            outline_rename_.buffer[n] = '\0';
            // Make sure the panel is visible — surprising to arm
            // a rename you can't see.
            menu_state_.show_outline_panel = true;
        }
    }
}

void App::handle_menu_actions(const noted::ui::widget::MenuBarResult& menu) {
    if (menu.quit_requested) {
        // Loop-top intercept will catch the dirty case; clean docs
        // exit immediately on the next iteration.
        window_->set_should_close(true);
    }
    if (menu.file_new_requested) {
        if (session_.is_dirty()) {
            prompt_.arm(DirtyPrompt::PendingAction::new_doc);
        } else {
            session_.reset_to_blank();
        }
    }
    if (menu.file_open_requested) {
        auto picked = noted::platform::io::pick_noted_open();
        if (!picked) {
            std::cerr << picked.error().format() << '\n';
        } else if (picked->has_value()) {
            if (auto r = session_.open_from(**picked); !r) {
                std::cerr << r.error().format() << '\n';
            }
        }
    }
    if (menu.file_save_requested && session_.has_path()) {
        if (auto r = session_.save_to(*session_.current_path()); !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (menu.file_save_as_requested) {
        const std::string default_name =
            session_.has_path() ? session_.current_path()->filename().string() : "untitled.noted";
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
            if (auto r = session_.save_to(target); !r) {
                std::cerr << r.error().format() << '\n';
            }
        }
    }
    if (menu.undo_requested) {
        if (auto r = session_.undo(); !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (menu.redo_requested) {
        if (auto r = session_.redo(); !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (menu.add_block_requested) {
        // Choose the parent: selected block if it's a group, else
        // the document root, else invalid_block_id which makes
        // AddBlockCommand allocate a fresh root.
        const auto& doc = session_.document();
        const auto selected = session_.selected_block();
        noted::domain::BlockId parent = noted::domain::invalid_block_id;
        if (selected != noted::domain::invalid_block_id) {
            if (const auto* node = doc.find(selected);
                node != nullptr && node->kind == noted::domain::BlockKind::group) {
                parent = selected;
            } else if (doc.root() != noted::domain::invalid_block_id) {
                parent = doc.root();
            }
        } else if (doc.root() != noted::domain::invalid_block_id) {
            parent = doc.root();
        }
        auto cmd = std::make_unique<noted::domain::AddBlockCommand>(
            *menu.add_block_requested, parent, std::string{});
        auto* cmd_ptr = cmd.get();
        if (auto r = session_.execute(std::move(cmd)); !r) {
            std::cerr << r.error().format() << '\n';
        } else {
            session_.set_selected_block(cmd_ptr->assigned_id());
        }
    }
}

void App::draw_widgets() {
    // After Phase R.4 every per-frame UI call (panels, command
    // dispatches off widget results, stroke engine + tool router
    // syncs) lives in `ui_panels_->draw()`. App is no longer
    // responsible for ordering these — that contract sits in
    // `UiPanels`, where the comments explaining each step live.
    ui_panels_->draw();

    // Brush slider-drift detection. The library card highlight is
    // honest only while the live PenOptions match the snapshot
    // captured at the last preset apply — once the user touches a
    // slider / colour picker the highlight should fade so a glance
    // at the panel can't lie about what's currently brushing.
    //
    // Exact equality is fine: ImGui sliders commit precise values,
    // and a tiny user wiggle that round-trips to the original byte
    // pattern is genuinely "still on the preset" by any practical
    // definition.
    if (active_brush_preset_ != noted::domain::tool::invalid_brush_preset_id &&
        !(tools_.pen == pen_at_last_apply_)) {
        active_brush_preset_ = noted::domain::tool::invalid_brush_preset_id;
    }
}

void App::refresh_window_title_if_changed() {
    auto t = session_.title();
    if (t != last_window_title_) {
        last_window_title_ = std::move(t);
        window_->set_title(last_window_title_);
    }
}

auto App::render_one_frame() -> noted::Result<void> {
    // Per-frame draw delegates entirely to the `RenderPasses`
    // subsystem after Phase R.3. App's residual responsibility on
    // the render path is the OUT_OF_DATE / SUBOPTIMAL recovery —
    // owner work that touches the swapchain handle, reallocates
    // canvas + strokes_target, and re-binds descriptors.
    //
    // Slice 2 of ADR 0033: before the render, make sure the canvas
    // is large enough to contain the entire page stack. The check
    // is cheap (one comparison); the wait_idle + resize path only
    // fires when pages were added/removed/loaded.
    if (auto r = ensure_canvas_fits_pages(); !r) {
        return std::unexpected(std::move(r).error());
    }
    auto rr = render_passes_->render_frame();
    if (!rr) {
        const auto code = rr.error().code;
        if (code == noted::ErrorCode::gpu_swapchain_out_of_date ||
            code == noted::ErrorCode::gpu_swapchain_suboptimal) {
            return recreate_swapchain();
        }
        return std::unexpected(std::move(rr).error());
    }
    return {};
}

auto App::recreate_swapchain() -> noted::Result<void> {
    device_->wait_idle();
    const auto [w, h] = window_->framebuffer_size();
    if (w == 0 || h == 0) {
        return {};
    }
    if (auto r = swapchain_->recreate(*physical_,
                                      *surface_,
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
    if (auto r = renderer_->rebind_swapchain(*device_, *swapchain_); !r) {
        return std::unexpected(std::move(r).error());
    }
    // Resize canvas + strokes to the desired extent that fits both
    // the new swapchain AND the page stack. This is the same extent
    // `ensure_canvas_fits_pages` would compute; calling it here
    // collapses the OUT_OF_DATE recovery into a single resize.
    const auto desired = desired_canvas_extent();
    if (auto r = canvas_->resize(*allocator_, desired); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = strokes_target_->resize(*allocator_, desired); !r) {
        return std::unexpected(std::move(r).error());
    }
    // Image views are fresh after resize — re-point both descriptors.
    noted::gpu::DescriptorWriter{canvas_set_}
        .write_combined_image_sampler(0, canvas_->view(), sampler_->handle())
        .commit();
    noted::gpu::DescriptorWriter{strokes_set_}
        .write_combined_image_sampler(0, strokes_target_->view(), sampler_->handle())
        .commit();
    // The camera's canvas extent must match the offscreen target;
    // the window extent stays at the swapchain extent (the composite
    // shader transforms canvas → window via these two).
    const auto sc_ext = swapchain_->summary().extent;
    camera_.set_canvas_extent(desired.width, desired.height);
    camera_.set_window_extent(sc_ext.width, sc_ext.height);
    return {};
}

auto App::desired_canvas_extent() const noexcept -> VkExtent2D {
    // Vulkan promises 4096 in either dimension on every conformant
    // device, and 16384 on every desktop GPU shipped this decade.
    // 16384 covers ~20 stacked Letter pages — beyond that the user
    // should switch to ADR 0034's direct-to-swapchain model.
    constexpr std::uint32_t kMaxDim = 16384U;
    // Comfortable margin around the page stack so the 24-px shadow
    // apron + a bit of breathing room are never clipped.
    constexpr float kPageMarginPx = 200.0F;

    const auto sc_ext = swapchain_->summary().extent;
    std::uint32_t w = sc_ext.width;
    std::uint32_t h = sc_ext.height;

    // Pages stack vertically with `gap_px` between them. Each page's
    // origin_x may also be non-zero (newly-added pages are centred
    // horizontally). Use the rightmost edge + margin as the width
    // requirement; the total stacked height + margin as the height
    // requirement.
    const auto& pages = session_.document().pages();
    if (!pages.empty()) {
        float right_edge = 0.0F;
        for (const auto& p : pages.pages()) {
            const float r = p.origin_x_px + p.extent_w_px;
            if (r > right_edge) {
                right_edge = r;
            }
        }
        const auto needed_w =
            static_cast<std::uint32_t>(std::max(0.0F, right_edge + kPageMarginPx));
        const auto needed_h =
            static_cast<std::uint32_t>(std::max(0.0F, pages.total_height_px() + kPageMarginPx));
        if (needed_w > w) {
            w = needed_w;
        }
        if (needed_h > h) {
            h = needed_h;
        }
    }

    if (w > kMaxDim) {
        w = kMaxDim;
    }
    if (h > kMaxDim) {
        h = kMaxDim;
    }
    if (w == 0U) {
        w = 1U;
    }
    if (h == 0U) {
        h = 1U;
    }
    return {.width = w, .height = h};
}

auto App::ensure_canvas_fits_pages() -> noted::Result<void> {
    if (!canvas_.has_value() || !strokes_target_.has_value()) {
        return {};  // not initialised yet
    }
    const auto desired = desired_canvas_extent();
    const auto current = canvas_->extent();
    if (current.width != desired.width || current.height != desired.height) {
        // Wait for any in-flight frame to finish — the canvas image
        // is still referenced by the previous frame's command buffer
        // until the per-frame fence has been waited on. wait_idle is
        // the sledgehammer; the cost is acceptable because this path
        // only runs on page-list change or window resize, not per
        // frame.
        device_->wait_idle();
        if (auto r = canvas_->resize(*allocator_, desired); !r) {
            return std::unexpected(std::move(r).error());
        }
        if (auto r = strokes_target_->resize(*allocator_, desired); !r) {
            return std::unexpected(std::move(r).error());
        }
        noted::gpu::DescriptorWriter{canvas_set_}
            .write_combined_image_sampler(0, canvas_->view(), sampler_->handle())
            .commit();
        noted::gpu::DescriptorWriter{strokes_set_}
            .write_combined_image_sampler(0, strokes_target_->view(), sampler_->handle())
            .commit();
    }
    // Sync the camera + stroke engine's canvas-size knowledge to the
    // **offscreen target** extent, NOT the window. After ADR 0033's
    // Slice 2 the offscreen canvas can be taller than the window, so
    // the previous framebuffer-resize auto-handlers (CameraController
    // + StrokeEngine listening to FramebufferResized) end up with the
    // wrong value and would misalign pointer-driven ink against the
    // rendered ribbon. Re-asserting from the host every frame keeps
    // App as the single source of truth for canvas extent — the cost
    // is a handful of double assignments. Window extent stays at the
    // swapchain extent and is updated in `recreate_swapchain`.
    camera_.set_canvas_extent(desired.width, desired.height);
    if (stroke_engine_) {
        stroke_engine_->set_canvas_size(desired);
    }
    return {};
}

auto App::save_for_dirty_prompt() -> bool {
    if (session_.has_path()) {
        if (auto r = session_.save_to(*session_.current_path()); !r) {
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
    if (auto r = session_.save_to(target); !r) {
        std::cerr << r.error().format() << '\n';
        return false;
    }
    return true;
}

void App::run_image_picker() {
    auto picked = noted::platform::io::pick_image_open();
    if (!picked) {
        std::cerr << picked.error().format() << '\n';
        return;
    }
    if (!picked->has_value()) {
        return;  // user cancelled
    }
    const auto path = **picked;

    // Decode the file just to read its intrinsic dimensions; the
    // decoded RGBA bytes are discarded here (B.7.b.2b's GPU upload
    // path will re-decode at upload time). Using `load_rgba8`
    // rather than a header-only `stbi_info` because the helper is
    // already in `platform::image_io` and the cost on the UI thread
    // for a typical photo is negligible.
    auto loaded = noted::platform::image_io::load_rgba8(path);
    if (!loaded) {
        std::cerr << loaded.error().format() << '\n';
        return;
    }

    // Register a fresh asset on the document. Direct mutation
    // (no command) for now — undo coverage for asset lifecycle
    // ships with B.7.b.2b's GPU upload work, when the lifecycle
    // becomes user-visible enough to warrant it.
    noted::domain::ImageAsset asset{};
    asset.source_path = path.string();
    asset.intrinsic_w_px = loaded->width;
    asset.intrinsic_h_px = loaded->height;
    const auto id = session_.document().image_assets_mut().allocate(std::move(asset));

    // Stamp the queued asset id + intrinsic dimensions onto the
    // Image tool's options. The next click-to-place commits an
    // `ImagePrimitive` carrying this asset id at this footprint;
    // the user can still scale via the size sliders afterwards.
    tools_.image.pending_asset_id = id;
    tools_.image.width_px = static_cast<float>(loaded->width);
    tools_.image.height_px = static_cast<float>(loaded->height);
}

void App::execute_pending_dirty_action(DirtyPrompt::PendingAction action) {
    switch (action) {
        case DirtyPrompt::PendingAction::quit:
            window_->set_should_close(true);
            break;
        case DirtyPrompt::PendingAction::new_doc:
            session_.reset_to_blank();
            break;
        case DirtyPrompt::PendingAction::none:
            break;
    }
}

}  // namespace noted::app
