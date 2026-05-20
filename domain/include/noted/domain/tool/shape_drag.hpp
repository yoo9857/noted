#pragma once

// Shape-tool drag logic — pure data + pure logic for the Shape tool.
//
// Phase B.5 of the unified-canvas plan. The first tool to land under
// the fully-decomposed App pattern: a `ShapeToolHandler` wraps these
// helpers and a `shape_overlay` widget renders the result.
//
// What's here:
//   - `ShapeKind` enum (wire-stable) — rectangle + ellipse for B.5.
//     Future kinds (line, polygon, polyline, star) append, never
//     reorder.
//   - `ShapeOptions` — user-tweakable parameters bound to UI
//     sliders/colour-pickers on `ToolState::shape`. Defaults are
//     a 2 px black outline on rectangles.
//   - `ShapePrimitive` — committed shape. Visual parameters are
//     **snapshotted at commit time** so a later slider tweak doesn't
//     retroactively repaint already-committed shapes (same
//     discipline as `Stroke::mode` from Phase B.2).
//   - `bounds_from_drag` + `shape_from_drag` — pure functions that
//     convert pointer drag spans into a `ShapePrimitive`. Empty drags
//     (sub-pixel clicks) collapse to a `nullopt` so a stationary
//     click doesn't litter the canvas with zero-area shapes.
//
// All coordinates are canvas pixels (post-Camera unprojection). The
// handler is responsible for screen → canvas conversion before
// calling these helpers; the router does that conversion centrally.
//
// What's NOT here (deliberate):
//   - Persistence to `.noted`. v0.x stores shapes in an App-owned
//     vector; future PR promotes them to a `Document::shapes()` list
//     wrapped in commands, same way pages were wired in Phase A.3.d.
//   - GPU rendering. The overlay widget renders shapes via ImGui's
//     background draw list — sufficient for v0.x. A future polygon
//     tessellator + per-shape GPU pipeline would replace that path
//     once shape count / per-frame cost matters.

#include <cstdint>
#include <optional>
#include <tuple>

namespace noted::domain::tool {

// Wire-stable shape ordinals. `.noted` will persist these once
// shapes move into the document; new kinds append.
enum class ShapeKind : std::uint8_t {
    rectangle = 0,
    ellipse = 1,
};

// User-tweakable shape parameters. Live on `ToolState::shape`; the
// `brush_options` widget renders sliders + colour pickers that
// mutate these in place. Snapshotted into `ShapePrimitive` at commit
// time so mid-document slider tweaks don't repaint committed shapes.
struct ShapeOptions {
    ShapeKind kind{ShapeKind::rectangle};

    // Outline colour (straight alpha — premultiplication happens in
    // the overlay if needed). Default = opaque black.
    float stroke_r{0.0F};
    float stroke_g{0.0F};
    float stroke_b{0.0F};
    float stroke_a{1.0F};

    // Outline thickness in canvas pixels. 0.5 px floor so a stray
    // negative input doesn't produce an invisible shape.
    float stroke_width_px{2.0F};

    [[nodiscard]] auto operator==(const ShapeOptions&) const noexcept -> bool = default;
};

// One committed shape — what the overlay renders + what a future
// `.noted` writer would persist. All fields are values (no
// references) so the primitive is trivially copyable + cheap to
// store in a `std::vector`.
struct ShapePrimitive {
    ShapeKind kind{ShapeKind::rectangle};

    // Bounds in canvas pixels. Canonicalized so `x0 < x1` and
    // `y0 < y1` — the helper sorts them on construction.
    double x0{0.0};
    double y0{0.0};
    double x1{0.0};
    double y1{0.0};

    // Visual parameters snapshotted from `ShapeOptions` at commit
    // time. Carried per-shape so toggling options never rewrites
    // history.
    float stroke_r{0.0F};
    float stroke_g{0.0F};
    float stroke_b{0.0F};
    float stroke_a{1.0F};
    float stroke_width_px{2.0F};

    [[nodiscard]] auto width() const noexcept -> double { return x1 - x0; }
    [[nodiscard]] auto height() const noexcept -> double { return y1 - y0; }
    [[nodiscard]] auto is_empty() const noexcept -> bool { return width() < 1.0 || height() < 1.0; }

    [[nodiscard]] auto operator==(const ShapePrimitive&) const noexcept -> bool = default;
};

// Build a canonical (x0, y0, x1, y1) span from a drag. Both
// orientations supported (drag bottom-right → top-left and vice
// versa produce the same bounds). Sub-pixel drags (Manhattan
// distance < 1 px on either axis) return `nullopt` so a stationary
// click doesn't commit a zero-area shape. NaN / inf coordinates also
// produce nullopt — a runaway pointer event can't poison the shape
// list.
[[nodiscard]] auto bounds_from_drag(double x1, double y1, double x2, double y2) noexcept
    -> std::optional<std::tuple<double, double, double, double>>;

// Combine drag coordinates + options into a complete `ShapePrimitive`.
// Returns `nullopt` for sub-pixel / NaN drags (same rule as
// `bounds_from_drag`). The options are read once; stroke width is
// clamped to a 0.5 px floor.
[[nodiscard]] auto shape_from_drag(
    double x1, double y1, double x2, double y2, const ShapeOptions& opt) noexcept
    -> std::optional<ShapePrimitive>;

}  // namespace noted::domain::tool
