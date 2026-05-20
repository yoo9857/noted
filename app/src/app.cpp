#include "app.hpp"

#define GLFW_INCLUDE_VULKAN

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "noted/compositor/layer_payload.hpp"
#include "noted/domain/command/commands.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/engine/harness/harness.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"
#include "noted/platform/fs/fs.hpp"
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

[[nodiscard]] auto glfw_required_extensions() -> std::span<const char* const> {
    std::uint32_t count = 0;
    const char** ptr = glfwGetRequiredInstanceExtensions(&count);
    return {ptr, static_cast<std::size_t>(count)};
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
    glfwSetWindowTitle(app->window_->native_handle(), app->last_window_title_.c_str());

    // Apply the configured default theme exactly once before the
    // first frame so the very first paint is already styled.
    app->applied_theme_ = app->cfg_.ui.default_theme;
    app->menu_state_.theme = app->cfg_.ui.default_theme;
    noted::ui::theme::apply(app->applied_theme_);

    // Seed the page-strip toggle from config — user can still flip
    // it at runtime via View → Page strip.
    app->menu_state_.show_page_strip = app->cfg_.ui.show_page_strip;

    // Seed the camera's canvas + window extents from the swapchain
    // before the first frame. The `framebuffer_resized` hook only
    // fires on subsequent resizes — without this the very first
    // render uses Camera's 1×1 defaults and the canvas collapses
    // to a single pixel.
    const auto extent = app->swapchain_->summary().extent;
    app->camera_.set_canvas_extent(extent.width, extent.height);
    app->camera_.set_window_extent(extent.width, extent.height);

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
        .selection = raw->selection_,
        .shapes = raw->session_.document().shapes(),
        .texts = raw->session_.document().texts(),
        .images = raw->images_,
        // images_ stays App-owned until B.7.b graduates it
        .prompt = raw->prompt_,
        .save_for_dirty_prompt = [raw]() -> bool { return raw->save_for_dirty_prompt(); },
        .execute_pending_dirty_action =
            [raw](DirtyPrompt::PendingAction a) { raw->execute_pending_dirty_action(a); },
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
        .surface_extensions = glfw_required_extensions(),
    });
    if (!instance) {
        return std::unexpected(std::move(instance).error());
    }
    instance_.emplace(std::move(*instance));

    auto raw_surface = create_window_surface(instance_->handle(), *window_);
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
    return {};
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
    auto imgui_host = noted::ui::ImGuiHost::create({
        .instance = &*instance_,
        .physical_device = &*physical_,
        .device = &*device_,
        .window = window_->native_handle(),
        .color_format = swapchain_->summary().color_format,
        .image_count = swapchain_->summary().image_count,
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
    // Phase B.7 — Image tool. Same pattern; placeholder rect for now,
    // real raster upload lands in B.7.b.
    auto image_handler = std::make_unique<noted::app::input::ImageToolHandler>(images_, tools_);
    image_handler_ = image_handler.get();
    tool_input_router_->register_handler(std::move(image_handler));
    tool_input_router_->set_active(tools_.active);
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
    glfwSetWindowShouldClose(window_->native_handle(), GLFW_FALSE);
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

    // ImGui frame setup happens BEFORE renderer.render_with_canvas
    // so ImGui::* calls below land in the same frame the renderer
    // will draw. finalize_frame is called unconditionally so a
    // swapchain-out-of-date error doesn't leave the frame dangling.
    imgui_host_->begin_frame();

    const noted::ui::widget::MenuBarStatus menu_status{
        .can_undo = session_.undo_stack().can_undo(),
        .can_redo = session_.undo_stack().can_redo(),
        .has_document_path = session_.has_path(),
    };
    auto menu = noted::ui::widget::menu_bar(menu_state_, menu_status);
    apply_theme_if_changed();
    wire_keyboard_shortcuts(menu_status, menu);
    handle_menu_actions(menu);

    draw_widgets();
    refresh_window_title_if_changed();

    imgui_host_->finalize_frame();

    if (auto r = render_one_frame(); !r) {
        std::cerr << r.error().format() << '\n';
        // Render fatal — force loop exit by signalling confirmed
        // exit so should_break_loop returns true regardless of
        // dirty state. Better than crashing with state mid-flight.
        glfwSetWindowShouldClose(window_->native_handle(), GLFW_TRUE);
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
        glfwSetWindowShouldClose(window_->native_handle(), GLFW_TRUE);
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
}

void App::refresh_window_title_if_changed() {
    auto t = session_.title();
    if (t != last_window_title_) {
        last_window_title_ = std::move(t);
        glfwSetWindowTitle(window_->native_handle(), last_window_title_.c_str());
    }
}

auto App::render_one_frame() -> noted::Result<void> {
    // Per-frame draw delegates entirely to the `RenderPasses`
    // subsystem after Phase R.3. App's residual responsibility on
    // the render path is the OUT_OF_DATE / SUBOPTIMAL recovery —
    // owner work that touches the swapchain handle, reallocates
    // canvas + strokes_target, and re-binds descriptors.
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
    if (auto r = canvas_->resize(*allocator_, swapchain_->summary().extent); !r) {
        return std::unexpected(std::move(r).error());
    }
    if (auto r = strokes_target_->resize(*allocator_, swapchain_->summary().extent); !r) {
        return std::unexpected(std::move(r).error());
    }
    // Image views are fresh after resize — re-point both descriptors.
    noted::gpu::DescriptorWriter{canvas_set_}
        .write_combined_image_sampler(0, canvas_->view(), sampler_->handle())
        .commit();
    noted::gpu::DescriptorWriter{strokes_set_}
        .write_combined_image_sampler(0, strokes_target_->view(), sampler_->handle())
        .commit();
    // Keep the camera's notion of window / canvas extent aligned
    // with the live swapchain. The framebuffer_resized hook also
    // fires for window resizes, but recreate_swapchain runs on the
    // OUT_OF_DATE recovery path too — covering both keeps the
    // camera's shader transform consistent.
    const auto ext = swapchain_->summary().extent;
    camera_.set_canvas_extent(ext.width, ext.height);
    camera_.set_window_extent(ext.width, ext.height);
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

void App::execute_pending_dirty_action(DirtyPrompt::PendingAction action) {
    switch (action) {
        case DirtyPrompt::PendingAction::quit:
            glfwSetWindowShouldClose(window_->native_handle(), GLFW_TRUE);
            break;
        case DirtyPrompt::PendingAction::new_doc:
            session_.reset_to_blank();
            break;
        case DirtyPrompt::PendingAction::none:
            break;
    }
}

}  // namespace noted::app
