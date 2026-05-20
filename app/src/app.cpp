#include "app.hpp"

#define GLFW_INCLUDE_VULKAN

#include <algorithm>
#include <array>
#include <cmath>
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
#include "noted/ui/widget/layer_panel.hpp"
#include "noted/ui/widget/outline_panel.hpp"
#include "noted/ui/widget/page_strip.hpp"
#include "noted/ui/widget/status_bar.hpp"

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

    const std::array<noted::gpu::DescriptorPoolSize, 1> pool_sizes{{{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
    }}};
    auto pool = noted::gpu::DescriptorPool::create(*device_,
                                                   noted::gpu::DescriptorPoolCreateInfo{
                                                       .max_sets = 1,
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

    auto canvas = noted::gpu::CanvasRenderTarget::create(*allocator_,
                                                         noted::gpu::CanvasCreateInfo{
                                                             .extent = swapchain_->summary().extent,
                                                             .format = kCanvasFormat,
                                                         });
    if (!canvas) {
        return std::unexpected(std::move(canvas).error());
    }
    canvas_.emplace(std::move(*canvas));

    noted::gpu::DescriptorWriter{canvas_set_}
        .write_combined_image_sampler(0, canvas_->view(), sampler_->handle())
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
    const auto canvas = swapchain_->summary().extent;
    const float page_w = cfg_.canvas.default_page_extent_w_px;
    const float page_h = cfg_.canvas.default_page_extent_h_px;
    const float origin_x = std::max(20.0F, (static_cast<float>(canvas.width) - page_w) * 0.5F);
    pages_ = noted::canvas::PageList{24.0F};
    constexpr std::array<noted::canvas::PageBackground, 3> kDemoBackgrounds{{
        noted::canvas::PageBackground::grid,
        noted::canvas::PageBackground::lined,
        noted::canvas::PageBackground::dotted,
    }};
    for (const auto bg : kDemoBackgrounds) {
        const auto idx = pages_.add_page(page_w, page_h, bg);
        pages_.set_page_origin_x(idx, origin_x);
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

    // Pointer position tracker. The scroll handler reads the latest
    // cursor pos so zoom anchors under the cursor (Goodnotes /
    // Procreate behaviour). Also drives middle-drag panning when
    // `panning_` is true.
    (void) noted::hook::registry().on_pointer_moved.subscribe(
        [this](const noted::hook::PointerMoved& e) {
            if (panning_) {
                const double dx = e.x - pan_last_x_;
                const double dy = e.y - pan_last_y_;
                camera_.translate_by(dx, dy);
            }
            cursor_x_ = e.x;
            cursor_y_ = e.y;
            pan_last_x_ = e.x;
            pan_last_y_ = e.y;
        });

    // Middle-button press / release toggles pan mode. Left button is
    // reserved for the stroke engine; right button is open for a
    // future context menu.
    (void) noted::hook::registry().on_pointer_pressed.subscribe(
        [this](const noted::hook::PointerPressed& e) {
            if (e.button != noted::hook::PointerButton::middle) {
                return;
            }
            panning_ = true;
            pan_last_x_ = e.x;
            pan_last_y_ = e.y;
        });
    (void) noted::hook::registry().on_pointer_released.subscribe(
        [this](const noted::hook::PointerReleased& e) {
            if (e.button != noted::hook::PointerButton::middle) {
                return;
            }
            panning_ = false;
        });

    // Scroll → zoom around the cursor. dy > 0 (wheel up) zooms in;
    // dy < 0 (wheel down) zooms out. Multiplicative step `1.1 ^ dy`
    // mirrors the perceptually-uniform feel of Photoshop / Figma.
    // The Camera's internal floor / ceiling absorbs any runaway dy.
    (void) noted::hook::registry().on_scrolled.subscribe([this](const noted::hook::Scrolled& e) {
        // Re-read `cfg_` every event so a future Preferences UI /
        // hot-reload can flip the feel without restart. The cost is
        // negligible — a handful of doubles on the stack per scroll.
        const double factor = std::pow(cfg_.canvas.zoom_step, e.dy);
        camera_.zoom_around(cursor_x_, cursor_y_, factor);
        camera_.clamp_scale(cfg_.canvas.zoom_min, cfg_.canvas.zoom_max);
    });

    // Framebuffer resize → keep camera's window extent in sync. The
    // canvas extent matches the swapchain extent today; if a future
    // PR makes them independent, this assignment splits.
    (void) noted::hook::registry().on_framebuffer_resized.subscribe(
        [this](const noted::hook::FramebufferResized& r) {
            camera_.set_window_extent(r.width, r.height);
            camera_.set_canvas_extent(r.width, r.height);
        });
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
    prompt_.draw([this]() -> bool { return save_for_dirty_prompt(); },
                 [this](DirtyPrompt::PendingAction a) { execute_pending_dirty_action(a); });

    noted::ui::widget::layer_panel(scene_->graph, &menu_state_.show_layer_panel);
    auto selected = session_.selected_block();
    noted::ui::widget::outline_panel(
        session_.document(), selected, &outline_rename_, &menu_state_.show_outline_panel);
    session_.set_selected_block(selected);

    // Page strip — observes pages_, surfaces user intent as a
    // PageStripResult. Sequenced as: apply structural mutations
    // first (add / remove), then resolve focus against the now-
    // current list. add_request + focus_request can co-occur if
    // the user double-clicked; mutate-then-focus keeps the index
    // semantics sane (focus_request always refers to the post-add
    // list because no remove competes in the same frame).
    auto strip = noted::ui::widget::page_strip(pages_, &menu_state_.show_page_strip);
    if (strip.add_request) {
        const auto idx = pages_.add_page(cfg_.canvas.default_page_extent_w_px,
                                         cfg_.canvas.default_page_extent_h_px,
                                         cfg_.canvas.default_page_background);
        // Match the demo seed's centring so newly added pages line
        // up with the existing ones at the identity camera transform.
        const auto canvas = swapchain_->summary().extent;
        const float origin_x = std::max(
            20.0F,
            (static_cast<float>(canvas.width) - cfg_.canvas.default_page_extent_w_px) * 0.5F);
        pages_.set_page_origin_x(idx, origin_x);
    }
    if (strip.remove_request) {
        pages_.remove_page(*strip.remove_request);
    }
    if (strip.focus_request) {
        // Park the focused page just below the menu bar with a bit
        // of breathing room. The current camera translation_y is
        // returned unchanged if the index is now stale (e.g. the
        // page got removed in the same frame), so this is safe.
        constexpr double kFocusTargetScreenY = 80.0;
        const double new_y =
            noted::ui::widget::camera_translation_y_for_page(pages_,
                                                             *strip.focus_request,
                                                             camera_.translation_y(),
                                                             camera_.scale(),
                                                             kFocusTargetScreenY);
        camera_.set_translation(camera_.translation_x(), new_y);
    }

    // Drain rename outputs into the command stream. Both flags are
    // mutually exclusive in practice (the widget never sets both in
    // the same frame); check Commit first so an Enter + Escape race
    // resolves to "save what was typed."
    if (outline_rename_.commit_requested) {
        outline_rename_.commit_requested = false;
        const auto target = outline_rename_.target;
        outline_rename_.target = noted::domain::invalid_block_id;
        std::string new_name{outline_rename_.buffer.data()};  // null-terminated by ImGui
        auto cmd = std::make_unique<noted::domain::SetNameCommand>(target, std::move(new_name));
        if (auto r = session_.execute(std::move(cmd)); !r) {
            std::cerr << r.error().format() << '\n';
        }
    }
    if (outline_rename_.cancel_requested) {
        outline_rename_.cancel_requested = false;
        outline_rename_.target = noted::domain::invalid_block_id;
    }

    noted::ui::widget::debug_overlay(
        noted::ui::widget::DebugOverlayInputs{
            .frame_index = engine_.frame_index(),
            .fallback_count = layer_compositor_->fallback_count(),
            .camera_scale = camera_.scale(),
            .camera_translation_x = camera_.translation_x(),
            .camera_translation_y = camera_.translation_y(),
        },
        debug_overlay_state_,
        &menu_state_.show_debug_overlay);

    if (menu_state_.show_demo_window) {
        ImGui::ShowDemoWindow(&menu_state_.show_demo_window);
    }
    if (menu_state_.show_about_window) {
        ImGui::SetNextWindowSize({340.0F, 0.0F}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(
                "About noted", &menu_state_.show_about_window, ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextUnformatted("noted — note-taking + raster editor");
            ImGui::TextUnformatted("노트 + 래스터 이미지 에디터");
            ImGui::TextUnformatted("v0.x development build");
            ImGui::Spacing();
            ImGui::TextDisabled("Engine: C++23 + Vulkan 1.4 + Slang");
            ImGui::TextDisabled("UI: Dear ImGui (ADR 0027)");
        }
        ImGui::End();
    }
    noted::ui::widget::status_bar({
        .frame_index = engine_.frame_index(),
        .zoom_pct = static_cast<float>(camera_.scale() * 100.0),
    });
}

void App::refresh_window_title_if_changed() {
    auto t = session_.title();
    if (t != last_window_title_) {
        last_window_title_ = std::move(t);
        glfwSetWindowTitle(window_->native_handle(), last_window_title_.c_str());
    }
}

auto App::render_one_frame() -> noted::Result<void> {
    // Canvas pass clears to opaque black — the source texture will
    // overdraw the whole surface, but defending against pipeline-
    // state surprises is cheap.
    VkClearColorValue canvas_clear{};
    canvas_clear.float32[0] = 0.0F;
    canvas_clear.float32[1] = 0.0F;
    canvas_clear.float32[2] = 0.0F;
    canvas_clear.float32[3] = 1.0F;

    // Swapchain pass clears to a dark teal — what shows through if
    // the composite quad ever leaves edges blank (it doesn't today,
    // but it's a useful regression tell).
    VkClearColorValue swapchain_clear{};
    swapchain_clear.float32[0] = 0.05F;
    swapchain_clear.float32[1] = 0.05F;
    swapchain_clear.float32[2] = 0.10F;
    swapchain_clear.float32[3] = 1.0F;

    auto canvas_cb = [this](VkCommandBuffer cb, VkExtent2D ext) { record_canvas_pass(cb, ext); };
    auto swapchain_cb = [this](VkCommandBuffer cb, VkExtent2D ext) {
        record_swapchain_pass(cb, ext);
    };

    auto rr = renderer_->render_with_canvas(
        *device_,
        *swapchain_,
        *canvas_,
        noted::gpu::Renderer::CanvasPassDesc{canvas_clear, canvas_cb},
        noted::gpu::Renderer::SwapchainPassDesc{swapchain_clear, swapchain_cb});
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

void App::record_canvas_pass(VkCommandBuffer cb, VkExtent2D ext) {
    // Order matters:
    //   1. Page backgrounds — paper rectangles. The canvas pass's
    //      clear colour shows through everywhere outside the pages.
    //   2. Layer compositor — adjustment / image layers within
    //      pages (none today; demo payloads were dropped in
    //      A.3.b so pages stay visible).
    //   3. Stroke engine — vector ink ribbons on top.
    page_renderer_->render(cb, ext, pages_);
    layer_compositor_->composite(cb, ext, scene_->graph, scene_->store);
    stroke_engine_->record(cb, ext);
}

void App::record_swapchain_pass(VkCommandBuffer cb, VkExtent2D /*ext*/) {
    const auto layout_h = composite_pipeline_layout_->handle();
    const auto pipeline_h = composite_pipeline_->handle();
    const VkDescriptorSet set_h = canvas_set_.handle();
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_h);
    vkCmdBindDescriptorSets(cb,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout_h,
                            /*firstSet=*/0,
                            /*setCount=*/1,
                            &set_h,
                            /*dynamicOffsetCount=*/0,
                            nullptr);
    // Push the Camera-derived view transform that the composite
    // vertex shader applies to the fullscreen-triangle's vertices.
    // Layout must mirror `CompositePush` in `shaders/fullscreen.slang`
    // (float2 scale, float2 translation = 16 bytes).
    struct CompositePush {
        float scale[2];
        float translation[2];
    };
    CompositePush push{};
    push.scale[0] = camera_.shader_scale_x();
    push.scale[1] = camera_.shader_scale_y();
    push.translation[0] = camera_.shader_translation_x();
    push.translation[1] = camera_.shader_translation_y();
    vkCmdPushConstants(cb, layout_h, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(CompositePush), &push);

    vkCmdDraw(cb, /*vertexCount=*/3, /*instanceCount=*/1, 0, 0);
    // ImGui draws on top of the composited canvas. The surrounding
    // vkCmdBeginRendering (owned by the renderer) is the right
    // context for ImGui_ImplVulkan_RenderDrawData. finalize_frame()
    // was already called above; render_into just records the
    // cached draw data.
    imgui_host_->render_into(cb);
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
    // canvas->view() is now a fresh handle — re-point the descriptor.
    noted::gpu::DescriptorWriter{canvas_set_}
        .write_combined_image_sampler(0, canvas_->view(), sampler_->handle())
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
