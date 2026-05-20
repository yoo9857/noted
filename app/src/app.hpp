#pragma once

// App — the v0.x process-lifetime owner.
//
// Wraps everything `main()` used to construct as local optionals
// (engine + GPU stack + scene + UI session + per-frame state) into
// a single owner with a clean two-phase lifecycle:
//
//     auto app = App::create();   // all init failure surfaces here
//     return (*app)->run();       // frame loop until close
//
// `create()` returns `Result<std::unique_ptr<App>>` because App is
// **non-movable** by design: hook subscriptions installed during
// construction capture `this`, and moving would dangle the captured
// pointer. The heap allocation pays for itself by removing the
// dozens of "if (!x) { wait_idle; shutdown; return EXIT_FAILURE; }"
// branches that bloated main().
//
// Member declaration order is the **construction order** and matches
// Vulkan's resource-lifetime ordering rules — destruction (reverse
// declaration) tears the GPU stack down safely. `~App()` does the
// `device.wait_idle()` + `engine.shutdown()` dance before any
// optional<> destructor runs.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <vulkan/vulkan.h>

#include "noted/compositor/layer_compositor.hpp"
#include "noted/domain/selection/selection.hpp"
#include "noted/domain/tool/tool.hpp"
#include "noted/engine/canvas/camera.hpp"
#include "noted/engine/canvas/page_renderer.hpp"
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
#include "noted/engine/gpu/stroke_target.hpp"
#include "noted/engine/gpu/surface.hpp"
#include "noted/engine/gpu/swapchain.hpp"
#include "noted/engine/stroke/stroke_engine.hpp"
#include "noted/platform/window/window.hpp"
#include "noted/ui/imgui_host.hpp"
#include "noted/ui/theme/theme.hpp"
#include "noted/ui/widget/debug_overlay.hpp"
#include "noted/ui/widget/menu_bar.hpp"
#include "noted/ui/widget/outline_panel.hpp"

#include "config/app_config.hpp"
#include "frame/render_passes.hpp"
#include "input/camera_controller.hpp"
#include "input/selection_tool_handler.hpp"
#include "input/tool_input_router.hpp"
#include "scene/demo_scene.hpp"
#include "ui/dirty_prompt.hpp"
#include "ui/document_session.hpp"

namespace noted::app {

class App {
public:
    // `cfg` is consumed by-value — `App::create` stores the snapshot
    // and uses it during init. Default = `AppConfig::defaults()` so
    // tests / callers that don't care about config keep working.
    [[nodiscard]] static auto create(config::AppConfig cfg = config::AppConfig::defaults())
        -> Result<std::unique_ptr<App>>;

    App(const App&) = delete;
    auto operator=(const App&) -> App& = delete;
    App(App&&) = delete;
    auto operator=(App&&) -> App& = delete;
    ~App();

    // Main frame loop. Returns `EXIT_SUCCESS` (0) on clean exit,
    // `EXIT_FAILURE` (1) if an unrecoverable error bubbled up
    // through the renderer. Logs the underlying error to stderr
    // before returning failure.
    [[nodiscard]] auto run() -> int;

private:
    App() = default;

    // ---- Frame loop decomposition ---------------------------------------
    // Each method is short and named; `on_frame` is the orchestrator.

    // Loop-top: intercept GLFW close on a dirty document. Returns
    // true if the loop should break (the user has actually decided
    // to exit, or the doc is clean).
    [[nodiscard]] auto should_break_loop() -> bool;

    void on_frame();
    void apply_theme_if_changed();
    void wire_keyboard_shortcuts(const noted::ui::widget::MenuBarStatus& status,
                                 noted::ui::widget::MenuBarResult& menu);
    void handle_menu_actions(const noted::ui::widget::MenuBarResult& menu);
    void draw_widgets();
    void refresh_window_title_if_changed();
    [[nodiscard]] auto render_one_frame() -> noted::Result<void>;

    // The 4-pass canvas pipeline + per-frame orchestration lives in
    // `noted::app::frame::RenderPasses` after Phase R.3 (ADR 0032).
    // App's `render_one_frame` delegates to that subsystem; the only
    // App-side concern left in the render path is the
    // `recreate_swapchain` recovery (owner work — reallocates targets
    // + rebinds descriptors after a swapchain out-of-date).

    // Dirty-prompt callbacks installed into `prompt_.draw()`.
    [[nodiscard]] auto save_for_dirty_prompt() -> bool;
    void execute_pending_dirty_action(DirtyPrompt::PendingAction action);

    // Swapchain re-create on OUT_OF_DATE / SUBOPTIMAL.
    [[nodiscard]] auto recreate_swapchain() -> noted::Result<void>;

    // ---- Init steps used by create() ------------------------------------
    [[nodiscard]] auto init_engine_and_window() -> noted::Result<void>;
    [[nodiscard]] auto init_gpu_stack() -> noted::Result<void>;
    [[nodiscard]] auto init_canvas_pipeline() -> noted::Result<void>;
    [[nodiscard]] auto init_layer_compositor() -> noted::Result<void>;
    [[nodiscard]] auto init_page_renderer() -> noted::Result<void>;
    [[nodiscard]] auto init_stroke_engine() -> noted::Result<void>;
    [[nodiscard]] auto init_renderer_and_imgui() -> noted::Result<void>;
    void install_frame_hook();

    // ---- Members (declaration order = construction / destruction order)
    //
    // Vulkan rules:
    //   - VkDevice must outlive every VkPipeline / VkBuffer / VkImage /
    //     VkDescriptorSet / VkSemaphore / VkFence allocated from it.
    //   - VkInstance must outlive VkSurfaceKHR + VkDevice.
    //   - VkPipeline objects must die before VkPipelineLayout (handled
    //     internally by gpu wrappers via their own member ordering).
    //
    // Hence: engine → window → instance → surface → physical → device
    // → allocator → swapchain → leaf GPU objects → renderer → imgui_host.

    noted::engine::Engine engine_;
    std::optional<noted::platform::Window> window_;

    std::optional<noted::gpu::Instance> instance_;
    std::optional<noted::gpu::Surface> surface_;
    std::optional<noted::gpu::PhysicalDevice> physical_;
    std::optional<noted::gpu::Device> device_;
    std::optional<noted::gpu::Allocator> allocator_;
    std::optional<noted::gpu::Swapchain> swapchain_;

    std::optional<noted::gpu::Sampler> sampler_;
    std::optional<noted::gpu::DescriptorSetLayout> composite_set_layout_;
    std::optional<noted::gpu::DescriptorPool> composite_descriptor_pool_;
    // Two descriptor sets bound to two different sampled images:
    //   canvas_set_       — samples the canvas (used by the swapchain
    //                       composite pass).
    //   strokes_set_      — samples the strokes target (used by the
    //                       canvas overlay pass).
    // Both sets use the same layout + sampler.
    noted::gpu::DescriptorSet canvas_set_{};
    noted::gpu::DescriptorSet strokes_set_{};

    std::optional<noted::gpu::CanvasRenderTarget> canvas_;
    // Strokes target — second offscreen image, same extent + format as
    // the canvas. The stroke engine renders ink into here; the overlay
    // pass composites it onto the canvas. Decoupling these two layers
    // is what makes the destination-out eraser preserve the page
    // pattern. See ADR 0031.
    std::optional<noted::gpu::StrokeTarget> strokes_target_;

    std::optional<noted::gpu::ShaderModule> fullscreen_vs_;
    std::optional<noted::gpu::ShaderModule> fullscreen_ps_;
    std::optional<noted::gpu::PipelineLayout> composite_pipeline_layout_;
    // Two composite-shader pipelines built from the same `fullscreen.slang`:
    //   composite_pipeline_         — targets the swapchain format,
    //                                 opaque (canvas already opaque).
    //   composite_overlay_pipeline_ — targets the canvas format with
    //                                 SRC_OVER blend; samples
    //                                 strokes_target and blends onto
    //                                 the canvas in the overlay pass.
    std::optional<noted::gpu::GraphicsPipeline> composite_pipeline_;
    std::optional<noted::gpu::GraphicsPipeline> composite_overlay_pipeline_;

    std::optional<noted::gpu::ShaderModule> layer_vs_;
    std::optional<noted::gpu::ShaderModule> layer_ps_;
    std::optional<noted::compositor::LayerCompositor> layer_compositor_;
    std::optional<DemoScene> scene_;

    // Page rendering — Phase A.3.b. The page list is the document's
    // canvas layout (Phase A.3.d moved ownership into Document); the
    // renderer draws each page's paper background BEFORE the layer
    // compositor + stroke engine layer on top.
    std::optional<noted::gpu::ShaderModule> page_bg_vs_;
    std::optional<noted::gpu::ShaderModule> page_bg_ps_;
    std::optional<noted::canvas::PageRenderer> page_renderer_;

    std::optional<noted::gpu::ShaderModule> stamp_vs_;
    std::optional<noted::gpu::ShaderModule> stamp_ps_;
    // StrokeEngine is heap-allocated + non-movable per ADR 0015
    // (RAII hook subscriptions).
    std::unique_ptr<noted::stroke::StrokeEngine> stroke_engine_;

    std::optional<noted::gpu::Renderer> renderer_;
    std::optional<noted::ui::ImGuiHost> imgui_host_;

    // ---- Runtime config snapshot ----------------------------------------
    // Populated by `App::create` from the caller's config. Members
    // are read during init; values that flow into pipelines /
    // buffers (frames_in_flight, window size) are baked at startup
    // and not re-read mid-frame. Per-frame tunables (zoom step,
    // zoom clamps) are re-read so a future Preferences UI can flip
    // them live.
    config::AppConfig cfg_{};

    // ---- UI / session state ---------------------------------------------
    DocumentSession session_{};
    DirtyPrompt prompt_{};
    noted::ui::widget::MenuBarState menu_state_{};
    noted::ui::widget::DebugOverlayState debug_overlay_state_{};
    noted::ui::widget::OutlineRenameState outline_rename_{};
    noted::ui::theme::ThemeKind applied_theme_{noted::ui::theme::ThemeKind::dark};
    std::string last_window_title_{};

    // ---- Tool state ---------------------------------------------------
    // The active editing tool. Mutated by the tool palette widget; the
    // stroke engine re-reads its brush from `tool_settings_for_tool`
    // on every switch so Pen draws normally and Eraser draws with
    // destination-out blend. Per-tool option payloads (PenOptions /
    // EraserOptions / ...) live on `tools_` and the App pushes them
    // into the stroke engine every frame for live-edit.
    noted::domain::tool::ToolState tools_{};

    // ---- Selection state ----------------------------------------------
    // The committed selection — modified by the Select tool, eventually
    // consumed by future Copy / Cut / Delete / Fill commands AND piped
    // into the existing `compositor::SelectionRasterizer` →
    // `gpu::SelectionMask` (ADR 0021 / 0022) so the compositor can clip
    // per-pixel operations to the region the user marked.
    noted::domain::Selection selection_{};
    // In-flight drag — `nullopt` between drags. Populated on Left-down
    // while the Select tool is active; updated on PointerMoved; applied
    // to `selection_` on PointerReleased. (Drag state now lives on the
    // `SelectionToolHandler` after Phase R.1 — see `tool_input_router_`
    // below.)

    // ---- Tool input routing -------------------------------------------
    // Owns the LEFT-button pointer subscriptions and dispatches to the
    // registered handler matching the active tool. Heap-allocated for
    // stable `this` (lambdas inside router's hook subscriptions
    // capture it). Built in `install_frame_hook` after the engine /
    // hook registry is alive.
    std::unique_ptr<noted::app::input::ToolInputRouter> tool_input_router_;
    // Non-owning — owned by the router. Cached so `selection_overlay`
    // can read `current_drag()` per frame without walking the handler
    // list.
    noted::app::input::SelectionToolHandler* selection_handler_{nullptr};

    // ---- Canvas view --------------------------------------------------
    // Pan + scale state shared by the composite pass (camera-projected
    // canvas quad), the stroke engine (input unprojection), and the
    // status / debug overlays (zoom % readout).
    noted::canvas::Camera camera_{};
    // Pan + zoom + cursor tracking + framebuffer-resize all live on
    // `CameraController` after Phase R.2. Heap-allocated for stable
    // `this` (hook lambdas capture themselves); built in
    // install_frame_hook once the registry is alive.
    std::unique_ptr<noted::app::input::CameraController> camera_controller_;

    // ---- Render passes ------------------------------------------------
    // 4-pass canvas pipeline orchestration (Phase R.3 / ADR 0032).
    // Holds non-owning references to every GPU resource above;
    // constructed AFTER all init_* steps so the references are
    // stable. App owns `recreate_swapchain` (touches the resources
    // themselves) and delegates the per-frame draw to
    // `render_passes_->render_frame()`.
    std::unique_ptr<noted::app::frame::RenderPasses> render_passes_;
};

}  // namespace noted::app
