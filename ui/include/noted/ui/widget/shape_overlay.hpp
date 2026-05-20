#pragma once

// Shape overlay — renders committed shapes + the in-progress drag
// preview on top of the canvas via ImGui's background draw list.
//
// Phase B.5. Mirrors the `selection_overlay` pattern: non-
// interactive screen-space decoration that takes a canvas→screen
// projection callback so it doesn't need a Camera dependency.
//
// Coordinate plumbing: shapes store canvas-pixel bounds; the
// overlay projects each corner through the supplied callback before
// drawing. This keeps the widget testable against a fake projection
// and pure-UI.

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "noted/domain/tool/shape_drag.hpp"

namespace noted::ui::widget {

// Canvas-pixel (x, y) → screen-pixel (x, y). Same shape as the
// callback used by `selection_overlay`.
using ShapeCanvasToScreenFn = std::function<std::pair<float, float>(double, double)>;

// In-flight drag preview in canvas-space, plus the snapshotted
// options. `nullopt` between drags.
struct ShapeDragPreview {
    double press_canvas_x{0.0};
    double press_canvas_y{0.0};
    double current_canvas_x{0.0};
    double current_canvas_y{0.0};
    noted::domain::tool::ShapeOptions options{};
};

// Render every committed shape + the active drag preview. `enabled`
// lets the host hide the overlay via the View menu without dropping
// the shapes from memory.
void shape_overlay(const std::vector<noted::domain::tool::ShapePrimitive>& shapes,
                   const std::optional<ShapeDragPreview>& preview,
                   const ShapeCanvasToScreenFn& canvas_to_screen,
                   bool enabled);

}  // namespace noted::ui::widget
