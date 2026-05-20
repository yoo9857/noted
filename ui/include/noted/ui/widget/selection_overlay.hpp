#pragma once

// Selection overlay — draws the active selection's bounds + the
// in-progress drag rectangle (if any) on top of the canvas.
//
// Uses ImGui's background draw list so the outline renders above the
// canvas but below every floating ImGui window. No widget window —
// the overlay is non-interactive screen-space decoration.
//
// Coordinate plumbing: the Selection stores rectangles in canvas
// pixels. The overlay receives a per-frame projection callable
// (`canvas_to_screen`) from the App so it doesn't need to know about
// `noted::canvas::Camera`. This keeps the widget testable against a
// fake projection without dragging in the engine.

#include <cstdint>
#include <functional>
#include <optional>

#include "noted/domain/selection/selection.hpp"

namespace noted::ui::widget {

// Canvas-pixel (x, y) → screen-pixel (x, y). Always provided by the
// App because the camera transform isn't in domain.
using CanvasToScreenFn = std::function<std::pair<float, float>(double, double)>;

// In-progress drag rectangle in canvas pixels — drawn dashed so the
// user sees what they're about to commit. `nullopt` between drags.
struct SelectionDragPreview {
    double press_canvas_x{0.0};
    double press_canvas_y{0.0};
    double current_canvas_x{0.0};
    double current_canvas_y{0.0};
};

// Render the overlay. Idempotent each frame — the widget reads + draws,
// owns no state. `selection` is the committed selection; `preview`
// is the active drag (nullopt when not dragging). `enabled` lets the
// host hide the overlay (e.g. the user disabled selection visibility
// via the View menu).
void selection_overlay(const noted::domain::Selection& selection,
                       const std::optional<SelectionDragPreview>& preview,
                       const CanvasToScreenFn& canvas_to_screen,
                       bool enabled);

}  // namespace noted::ui::widget
