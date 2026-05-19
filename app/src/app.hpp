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
#include "noted/engine/stroke/stroke_engine.hpp"
#include "noted/platform/window/window.hpp"
#include "noted/ui/imgui_host.hpp"
#include "noted/ui/theme/theme.hpp"
#include "noted/ui/widget/debug_overlay.hpp"
#include "noted/ui/widget/menu_bar.hpp"

#include "scene/demo_scene.hpp"
#include "ui/dirty_prompt.hpp"
#include "ui/document_session.hpp"

namespace noted::app {

class App {
public:
    [[nodiscard]] static auto create() -> Result<std::unique_ptr<App>>;

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

    // Renderer DrawCallback bodies, called from inside the
    // surrounding `vkCmdBeginRendering` the renderer owns.
    void record_canvas_pass(VkCommandBuffer cb, VkExtent2D extent);
    void record_swapchain_pass(VkCommandBuffer cb, VkExtent2D extent);

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
    noted::gpu::DescriptorSet canvas_set_{};

    std::optional<noted::gpu::CanvasRenderTarget> canvas_;

    std::optional<noted::gpu::ShaderModule> fullscreen_vs_;
    std::optional<noted::gpu::ShaderModule> fullscreen_ps_;
    std::optional<noted::gpu::PipelineLayout> composite_pipeline_layout_;
    std::optional<noted::gpu::GraphicsPipeline> composite_pipeline_;

    std::optional<noted::gpu::ShaderModule> layer_vs_;
    std::optional<noted::gpu::ShaderModule> layer_ps_;
    std::optional<noted::compositor::LayerCompositor> layer_compositor_;
    std::optional<DemoScene> scene_;

    std::optional<noted::gpu::ShaderModule> stamp_vs_;
    std::optional<noted::gpu::ShaderModule> stamp_ps_;
    // StrokeEngine is heap-allocated + non-movable per ADR 0015
    // (RAII hook subscriptions).
    std::unique_ptr<noted::stroke::StrokeEngine> stroke_engine_;

    std::optional<noted::gpu::Renderer> renderer_;
    std::optional<noted::ui::ImGuiHost> imgui_host_;

    // ---- UI / session state ---------------------------------------------
    DocumentSession session_{};
    DirtyPrompt prompt_{};
    noted::ui::widget::MenuBarState menu_state_{};
    noted::ui::widget::DebugOverlayState debug_overlay_state_{};
    noted::ui::theme::ThemeKind applied_theme_{noted::ui::theme::ThemeKind::dark};
    std::string last_window_title_{};
};

}  // namespace noted::app
