#pragma once

// ShapeToolHandler — input behaviour for the Shape tool.
//
// Phase B.5 of the unified-canvas plan. Owns the in-flight drag
// state; on release builds an `AddShapeCommand` and pushes it via
// the configured command sink (typically `DocumentSession::execute`
// so the addition lands in the undo stack and round-trips through
// `.noted` save / load).
//
// On press: snapshot the press point + `ToolOptions::shape` (the
// commit-time snapshot prevents mid-drag slider tweaks from
// retroactively changing the shape). On release: build a
// `ShapePrimitive` via `shape_from_drag` and emit an
// `AddShapeCommand` through the sink. On deactivate: discard the
// in-flight drag.
//
// The overlay widget reads `current_drag()` each frame for the
// dashed preview rectangle / ellipse.

#include <functional>
#include <memory>
#include <optional>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/tool/shape_drag.hpp"
#include "noted/domain/tool/tool.hpp"

#include "input/tool_input_handler.hpp"

namespace noted::app::input {

class ShapeToolHandler final : public ToolInputHandler {
public:
    // Called with a fully-built `AddShapeCommand`; the App-side
    // implementation passes it to `DocumentSession::execute` so the
    // mutation lands in the undo stack + dirty-tracker.
    using CommandSink = std::function<void(std::unique_ptr<noted::domain::Command>)>;

    ShapeToolHandler(CommandSink sink, const noted::domain::tool::ToolState& tools) noexcept;

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
        noted::domain::tool::ShapeOptions options{};
    };
    [[nodiscard]] auto current_drag() const noexcept -> std::optional<DragState> { return drag_; }

private:
    CommandSink sink_;
    const noted::domain::tool::ToolState& tools_;
    std::optional<DragState> drag_{};
};

}  // namespace noted::app::input
