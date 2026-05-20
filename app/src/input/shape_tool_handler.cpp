#include "input/shape_tool_handler.hpp"

#include <utility>

namespace noted::app::input {

ShapeToolHandler::ShapeToolHandler(CommandSink sink,
                                   const noted::domain::tool::ToolState& tools) noexcept
    : sink_(std::move(sink)), tools_(tools) {}

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
    // don't retroactively change THIS shape.
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
        shape.has_value() && sink_) {
        sink_(std::make_unique<noted::domain::AddShapeCommand>(*shape));
    }
    drag_.reset();
}

void ShapeToolHandler::on_deactivated() noexcept {
    // Mid-drag tool switch: discard the in-flight shape. Already-
    // committed shapes belong to the document, not the in-flight
    // drag.
    drag_.reset();
}

}  // namespace noted::app::input
