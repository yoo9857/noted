#include "input/shape_tool_handler.hpp"

namespace noted::app::input {

ShapeToolHandler::ShapeToolHandler(std::vector<noted::domain::tool::ShapePrimitive>& shapes,
                                   const noted::domain::tool::ToolState& tools) noexcept
    : shapes_(shapes), tools_(tools) {}

auto ShapeToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::shape;
}

void ShapeToolHandler::on_pressed(double cx, double cy, bool /*shift*/, bool /*alt*/) {
    DragState st{};
    st.press_x = cx;
    st.press_y = cy;
    st.current_x = cx;
    st.current_y = cy;
    // Snapshot the live options at PRESS so mid-drag slider edits
    // don't retroactively change THIS shape. Same discipline as
    // `Stroke::mode` from Phase B.2.
    st.options = tools_.shape;
    drag_ = st;
}

void ShapeToolHandler::on_moved(double cx, double cy) {
    if (!drag_.has_value()) {
        return;
    }
    drag_->current_x = cx;
    drag_->current_y = cy;
}

void ShapeToolHandler::on_released(double cx, double cy) {
    if (!drag_.has_value()) {
        return;
    }
    drag_->current_x = cx;
    drag_->current_y = cy;
    if (auto shape = noted::domain::tool::shape_from_drag(
            drag_->press_x, drag_->press_y, drag_->current_x, drag_->current_y, drag_->options);
        shape.has_value()) {
        shapes_.push_back(*shape);
    }
    drag_.reset();
}

void ShapeToolHandler::on_deactivated() noexcept {
    // Mid-drag tool switch: discard the in-flight shape. The
    // user's intent was a Shape-tool drag specifically; switching
    // away dismisses it. Already-committed shapes in `shapes_`
    // stay — they belong to the document, not the in-flight drag.
    drag_.reset();
}

}  // namespace noted::app::input
