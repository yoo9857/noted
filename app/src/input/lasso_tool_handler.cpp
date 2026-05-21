#include "input/lasso_tool_handler.hpp"

#include <cstdint>

#include "noted/domain/selection/selection.hpp"

namespace noted::app::input {

namespace {

// Sub-pixel canvas coords arrive as doubles. Quantise to int32 with
// rounding to keep the integer-pixel polygon faithful to the
// pointer path.
[[nodiscard]] auto quantise(double v) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(v >= 0.0 ? v + 0.5 : v - 0.5);
}

// Skip duplicate consecutive vertices — Qt + Win32 can both
// re-fire `mouseMoved` at the same screen pixel during a long
// hold; without dedup we'd grow the polygon's vertex list without
// adding any actual shape.
[[nodiscard]] auto same_as_last(const noted::domain::LassoPolygon& poly,
                                noted::domain::Point2i p) noexcept -> bool {
    return !poly.vertices().empty() && poly.vertices().back() == p;
}

}  // namespace

LassoToolHandler::LassoToolHandler(noted::domain::Selection& sel) noexcept : sel_(sel) {}

auto LassoToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::lasso;
}

void LassoToolHandler::on_pressed(double cx, double cy, bool shift, bool alt) {
    DragState s{};
    s.shift = shift;
    s.alt = alt;
    s.polygon.push({quantise(cx), quantise(cy)});
    drag_ = std::move(s);
}

void LassoToolHandler::on_moved(double cx, double cy) {
    if (!drag_) {
        return;
    }
    const noted::domain::Point2i p{quantise(cx), quantise(cy)};
    if (!same_as_last(drag_->polygon, p)) {
        drag_->polygon.push(p);
    }
}

void LassoToolHandler::on_released(double cx, double cy) {
    if (!drag_) {
        return;
    }
    const noted::domain::Point2i p{quantise(cx), quantise(cy)};
    if (!same_as_last(drag_->polygon, p)) {
        drag_->polygon.push(p);
    }
    // Commit to the selection. Modifier-driven boolean ops on
    // polygons (Shift = add, Alt = subtract) are a follow-up slice;
    // for now the polygon either replaces or unions with the
    // existing selection.
    //
    // - default → clear existing selection, add this polygon
    // - shift   → union (just add — rasterizer handles the union)
    if (!drag_->shift && !drag_->alt) {
        sel_.clear();
    }
    // alt subtract path: not implemented at v1 (polygon-poly
    // boolean is a non-trivial geometry problem; deferred).
    sel_.add_polygon(std::move(drag_->polygon));
    drag_.reset();
}

void LassoToolHandler::on_deactivated() noexcept {
    drag_.reset();
}

}  // namespace noted::app::input
