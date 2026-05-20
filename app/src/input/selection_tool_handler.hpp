#pragma once

// SelectionToolHandler — input behaviour for the Select tool.
//
// Phase R.1 (per ADR 0032). Moves the rectangle-drag logic that
// Phase B.4 added directly to App into a self-contained handler
// the `ToolInputRouter` dispatches to when the active tool is
// `ToolKind::select`. App now only needs to construct + register
// this handler at startup; the per-event subscriptions live on
// the router.
//
// Owns:
//   - A reference to `noted::domain::Selection` (the committed
//     selection the handler mutates on release).
//   - The in-flight drag state (an `optional<DragState>` so a
//     stationary tool with no drag has no allocated state).
//
// The overlay widget asks for `current_drag()` each frame to
// render the in-progress drag rectangle. Returning an inspector
// rather than emitting events to the App keeps the data-flow
// one-directional (handler holds truth; App queries on demand).

#include <optional>

#include "noted/domain/tool/selection_drag.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::domain {
class Selection;
}  // namespace noted::domain

namespace noted::app::input {

class SelectionToolHandler final : public ToolInputHandler {
public:
    explicit SelectionToolHandler(noted::domain::Selection& sel) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

    // In-flight drag, in canvas pixels. `nullopt` between drags.
    // The overlay widget reads this each frame to draw the dashed
    // preview rectangle. Returning by value (small POD) — keeps
    // the inspector const-correct and the consumer decoupled from
    // the handler's internal lifetime.
    struct DragState {
        double press_x{0.0};
        double press_y{0.0};
        double current_x{0.0};
        double current_y{0.0};
        noted::domain::tool::SelectionDragMode mode{
            noted::domain::tool::SelectionDragMode::replace};
    };
    [[nodiscard]] auto current_drag() const noexcept -> std::optional<DragState> { return drag_; }

private:
    noted::domain::Selection& sel_;
    std::optional<DragState> drag_{};
};

}  // namespace noted::app::input
