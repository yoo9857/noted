#pragma once

// LassoToolHandler — input behaviour for the Lasso tool.
//
// Free-form polygon selection: the user presses the left button,
// drags the pen around the region they want to enclose, and on
// release the polygon's vertices are added to the active
// `Selection`. Sampled at every PointerMoved event so the path
// follows the pen exactly; a future slice can decimate the
// vertex list (Douglas-Peucker) if the polygons get too dense
// for the rasterizer, but at v1 stroke-grade densities the
// scanline-fill cost is negligible.
//
// Modifier-key semantics (ADR — to-be-written, follows the
// Photoshop convention):
//   - default      → replace the selection with this polygon
//   - Shift held   → add: union the new polygon with the existing
//                    selection
//   - Alt held     → subtract: …
// Today only `replace` is wired; Shift / Alt route through the
// modifier flags but the `add` / `subtract` paths are stubs for
// the next slice (boolean polygon ops are a separate problem and
// the rasterizer handles the union of multiple polygons fine).
//
// Owns:
//   - A reference to `noted::domain::Selection` it mutates on
//     release.
//   - The in-flight polygon (built up vertex-by-vertex while the
//     drag is live).

#include <optional>

#include "noted/domain/selection/selection.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::domain {
class Selection;
}  // namespace noted::domain

namespace noted::app::input {

class LassoToolHandler final : public ToolInputHandler {
public:
    explicit LassoToolHandler(noted::domain::Selection& sel) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

    // In-flight polygon — `nullopt` between drags. The overlay
    // widget reads this each frame to draw the preview path.
    // Returning const-ref by `optional<const&>` not allowed, so we
    // return the raw pointer-or-null for the consumer to dereference.
    [[nodiscard]] auto in_flight() const noexcept -> const noted::domain::LassoPolygon* {
        return drag_.has_value() ? &drag_->polygon : nullptr;
    }

private:
    struct DragState {
        noted::domain::LassoPolygon polygon;
        bool shift{false};
        bool alt{false};
    };
    noted::domain::Selection& sel_;
    std::optional<DragState> drag_{};
};

}  // namespace noted::app::input
