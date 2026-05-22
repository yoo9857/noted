#pragma once

// Pressure curve editor — a 2D bezier handle editor that lets the
// artist sculpt the pressure-to-alpha response with the kind of
// precision Photoshop / Procreate / Clip Studio expose in their
// pen-dynamics panels.
//
// The widget renders a square graph (input pressure on x, output
// alpha on y). The cubic bezier curve is fixed at endpoints (0, 0)
// and (1, 1); the two free handles in the middle are draggable.
// Returns `true` when the user committed a change this frame so the
// host can refresh dependent UI state (e.g. clear the brush
// library's "active card" highlight if the curve drifted off
// preset).
//
// Pure ImGui — no engine/domain plumbing. The PressureCurve struct
// (engine/stroke header) is shared between this widget and the
// stroke engine; same type, no conversions.

#include "noted/engine/stroke/stroke_geometry.hpp"

namespace noted::ui::widget {

// Render the editor at the current cursor position. Returns true
// if the user dragged a handle this frame. `size_px` controls the
// widget's square dimension; a future tooltip / mini-mode would
// pass a smaller value.
[[nodiscard]] auto pressure_curve_editor(noted::stroke::PressureCurve& curve,
                                         float size_px = 160.0F) -> bool;

}  // namespace noted::ui::widget
