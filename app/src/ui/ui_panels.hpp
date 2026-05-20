#pragma once

// UiPanels — the per-frame UI orchestration.
//
// Phase R.4 of the App-layer decomposition (per ADR 0032). Owns
// the body of App's old `draw_widgets()`: every panel call, every
// per-frame state-sync push into the stroke engine + router, every
// command-triggering branch off a widget result. App's
// `draw_widgets()` becomes a one-line forward to `panels_.draw()`.
//
// **Non-owning.** Like R.3's RenderPasses, every dependency is a
// reference; App outlives UiPanels so the refs stay stable. The
// `Deps` struct names all 20 dependencies (including two
// `std::function`s for App-owned callbacks that drive the dirty-
// prompt) so the contract is explicit.
//
// What UiPanels does NOT do:
//   - File menu wiring (open / save / save-as dialogs, the GLFW
//     window-close flag) — those touch the window handle and stay
//     in App's `handle_menu_actions`.
//   - The render pass orchestration — that's R.3's RenderPasses.
//   - The input event routing — that's R.1's ToolInputRouter.
//   - The camera input — that's R.2's CameraController.
//   - The dirty-prompt's save / pending-action **callbacks** — they
//     bind App methods (`save_for_dirty_prompt`,
//     `execute_pending_dirty_action`) and are passed in via the
//     Deps struct as `std::function`s.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "noted/domain/selection/selection.hpp"
#include "noted/domain/tool/shape_drag.hpp"
#include "noted/domain/tool/tool.hpp"
#include "noted/engine/engine.hpp"
#include "noted/ui/widget/debug_overlay.hpp"
#include "noted/ui/widget/menu_bar.hpp"
#include "noted/ui/widget/outline_panel.hpp"

#include "config/app_config.hpp"
#include "scene/demo_scene.hpp"
#include "ui/dirty_prompt.hpp"
#include "ui/document_session.hpp"

namespace noted::canvas {
class Camera;
}  // namespace noted::canvas

namespace noted::compositor {
class LayerCompositor;
}  // namespace noted::compositor

namespace noted::gpu {
class Swapchain;
}  // namespace noted::gpu

namespace noted::stroke {
class StrokeEngine;
}  // namespace noted::stroke

namespace noted::app::input {
class SelectionToolHandler;
class ShapeToolHandler;
class ToolInputRouter;
}  // namespace noted::app::input

namespace noted::app::ui {

class UiPanels {
public:
    struct Deps {
        // ---- engine + session + scene ----------------------------------
        noted::engine::Engine& engine;
        noted::app::DocumentSession& session;
        noted::app::DemoScene& scene;
        noted::compositor::LayerCompositor& layer_compositor;

        // ---- input subsystems R.4 talks to -----------------------------
        noted::stroke::StrokeEngine& stroke_engine;
        noted::app::input::ToolInputRouter& tool_input_router;
        // Nullable — `nullptr` when the respective tool isn't
        // registered with the router.
        noted::app::input::SelectionToolHandler* selection_handler{nullptr};
        noted::app::input::ShapeToolHandler* shape_handler{nullptr};

        // Camera is non-const — the page-strip handler translates the
        // camera vertically when the user clicks a row or adds a page.
        noted::canvas::Camera& camera;
        const noted::gpu::Swapchain& swapchain;
        const noted::app::config::AppConfig& cfg;

        // ---- mutable UI state (App owns the storage) -------------------
        noted::ui::widget::MenuBarState& menu_state;
        noted::ui::widget::DebugOverlayState& debug_overlay_state;
        noted::ui::widget::OutlineRenameState& outline_rename;

        // ---- mutable tool + selection + shapes state -------------------
        noted::domain::tool::ToolState& tools;
        noted::domain::Selection& selection;
        // App owns the shapes vector; the ShapeToolHandler mutates it
        // on commit; the shape_overlay reads it each frame.
        const std::vector<noted::domain::tool::ShapePrimitive>& shapes;

        // ---- dirty-prompt + callbacks back into App --------------------
        noted::app::DirtyPrompt& prompt;
        // Returns true if the save succeeded (or the prompt should
        // proceed); false on user cancel or save failure.
        std::function<bool()> save_for_dirty_prompt;
        // Acts on whichever pending action the prompt resolved
        // (quit / new_doc).
        std::function<void(noted::app::DirtyPrompt::PendingAction)> execute_pending_dirty_action;
    };

    [[nodiscard]] static auto create(Deps deps) -> std::unique_ptr<UiPanels>;

    UiPanels(const UiPanels&) = delete;
    auto operator=(const UiPanels&) -> UiPanels& = delete;
    UiPanels(UiPanels&&) = delete;
    auto operator=(UiPanels&&) -> UiPanels& = delete;
    ~UiPanels() = default;

    // Per-frame draw — invokes every panel + state-sync push the
    // old `App::draw_widgets` performed, in the same order.
    void draw();

private:
    explicit UiPanels(Deps deps) noexcept;

    // The reference fields mirror `Deps` one-to-one; the
    // std::function fields are stored by value (move-constructed
    // in the ctor body) so the lambdas can capture App.
    noted::engine::Engine& engine_;
    noted::app::DocumentSession& session_;
    noted::app::DemoScene& scene_;
    noted::compositor::LayerCompositor& layer_compositor_;

    noted::stroke::StrokeEngine& stroke_engine_;
    noted::app::input::ToolInputRouter& tool_input_router_;
    noted::app::input::SelectionToolHandler* selection_handler_;
    noted::app::input::ShapeToolHandler* shape_handler_;

    noted::canvas::Camera& camera_;
    const noted::gpu::Swapchain& swapchain_;
    const noted::app::config::AppConfig& cfg_;

    noted::ui::widget::MenuBarState& menu_state_;
    noted::ui::widget::DebugOverlayState& debug_overlay_state_;
    noted::ui::widget::OutlineRenameState& outline_rename_;

    noted::domain::tool::ToolState& tools_;
    noted::domain::Selection& selection_;
    const std::vector<noted::domain::tool::ShapePrimitive>& shapes_;

    noted::app::DirtyPrompt& prompt_;
    std::function<bool()> save_for_dirty_prompt_;
    std::function<void(noted::app::DirtyPrompt::PendingAction)> execute_pending_dirty_action_;
};

}  // namespace noted::app::ui
