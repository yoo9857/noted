#include "input/selection_tool_handler.hpp"

#include "noted/domain/selection/selection.hpp"
#include "noted/domain/tool/selection_drag.hpp"

namespace noted::app::input {

SelectionToolHandler::SelectionToolHandler(noted::domain::Selection& sel) noexcept : sel_(sel) {}

auto SelectionToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::select;
}

void SelectionToolHandler::on_pressed(double cx, double cy, bool shift, bool alt) {
    DragState st{};
    st.press_x = cx;
    st.press_y = cy;
    st.current_x = cx;
    st.current_y = cy;
    st.mode = noted::domain::tool::drag_mode_from_modifiers(shift, alt);
    drag_ = st;
}

void SelectionToolHandler::on_moved(double cx, double cy) {
    if (!drag_.has_value()) {
        return;
    }
    drag_->current_x = cx;
    drag_->current_y = cy;
}

void SelectionToolHandler::on_released(double cx, double cy) {
    if (!drag_.has_value()) {
        return;
    }
    drag_->current_x = cx;
    drag_->current_y = cy;
    const auto rect = noted::domain::tool::rect_from_drag(
        drag_->press_x, drag_->press_y, drag_->current_x, drag_->current_y);
    noted::domain::tool::apply_drag(sel_, rect, drag_->mode);
    drag_.reset();
}

void SelectionToolHandler::on_deactivated() noexcept {
    // Mid-drag tool switch: discard the in-flight drag. Committing
    // a half-finished selection would be more surprising than
    // dropping it — the user's intent was specifically THIS tool's
    // drag, and they explicitly switched away.
    drag_.reset();
}

}  // namespace noted::app::input
