#pragma once

// ShapeToolHandler — input behaviour for the Shape tool.
//
// Phase B.5 of the unified-canvas plan. **First tool to land under
// the fully-decomposed App pattern** (per ADR 0032): one
// ToolInputHandler subclass + one `register_handler` line in
// `App::install_frame_hook`. App.cpp does not grow.
//
// Owns:
//   - A reference to the App's committed-shape vector (the
//     `std::vector<ShapePrimitive>&` that App declares — App owns
//     storage so the shape list survives the handler's lifetime if
//     a future refactor swaps handlers).
//   - A reference to `ToolState::shape` for the snapshot-at-commit
//     visual parameters.
//   - The in-flight drag state (`optional<DragState>`).
//
// On press: snapshot the press point + `ToolOptions::shape` (the
// commit-time snapshot prevents mid-drag slider tweaks from
// retroactively changing the shape). On release: build a
// `ShapePrimitive` via `shape_from_drag` and push into the vector.
// On deactivate: discard the in-flight drag.
//
// The overlay widget reads `current_drag()` each frame for the
// dashed preview rectangle/ellipse.

#include <optional>
#include <vector>

#include "noted/domain/tool/shape_drag.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::app::input {

class ShapeToolHandler final : public ToolInputHandler {
public:
    ShapeToolHandler(std::vector<noted::domain::tool::ShapePrimitive>& shapes,
                     const noted::domain::tool::ToolState& tools) noexcept;

    [[nodiscard]] auto handled_kind() const noexcept -> noted::domain::tool::ToolKind override;
    void on_pressed(double cx, double cy, bool shift, bool alt) override;
    void on_moved(double cx, double cy) override;
    void on_released(double cx, double cy) override;
    void on_deactivated() noexcept override;

    // In-flight drag. `nullopt` between drags. The overlay widget
    // reads this each frame to draw the preview rectangle / ellipse
    // with the snapshotted options applied.
    struct DragState {
        double press_x{0.0};
        double press_y{0.0};
        double current_x{0.0};
        double current_y{0.0};
        // Snapshotted at press time so a mid-drag slider tweak
        // doesn't retroactively repaint the preview. The committed
        // shape uses these same values.
        noted::domain::tool::ShapeOptions options{};
    };
    [[nodiscard]] auto current_drag() const noexcept -> std::optional<DragState> { return drag_; }

    // Committed shapes (read-only inspector for the overlay + future
    // file-format writer). The reference is the same storage the
    // handler mutates on `on_released`.
    [[nodiscard]] auto shapes() const noexcept
        -> const std::vector<noted::domain::tool::ShapePrimitive>& {
        return shapes_;
    }

private:
    std::vector<noted::domain::tool::ShapePrimitive>& shapes_;
    const noted::domain::tool::ToolState& tools_;
    std::optional<DragState> drag_{};
};

}  // namespace noted::app::input
