#include "input/text_tool_handler.hpp"

#include <cstring>

namespace noted::app::input {

TextToolHandler::TextToolHandler(std::vector<noted::domain::tool::TextPrimitive>& texts,
                                 const noted::domain::tool::ToolState& tools) noexcept
    : texts_(texts), tools_(tools) {}

auto TextToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::text;
}

void TextToolHandler::on_pressed(double cx, double cy, bool /*shift*/, bool /*alt*/) {
    // Click-while-editing → commit current then start new. Matches
    // every paint app's "no lost text" UX.
    if (editing_.has_value()) {
        commit_editing();
    }
    EditingState st{};
    st.position_x = cx;
    st.position_y = cy;
    st.options = tools_.text;  // snapshot at PRESS
    st.needs_focus = true;
    // Buffer initialized to all-zero by default initialization; no
    // memset needed.
    editing_ = st;
}

void TextToolHandler::on_moved(double /*cx*/, double /*cy*/) {
    // Text is click-to-anchor, not drag — no per-move work.
}

void TextToolHandler::on_released(double /*cx*/, double /*cy*/) {
    // The press-release pair anchors the editing session; the
    // ImGui InputText (rendered by the overlay) handles keys
    // between presses.
}

void TextToolHandler::on_deactivated() noexcept {
    // Switching away mid-typing commits whatever's there. Pure-
    // empty buffers are dropped silently by `commit_editing` so a
    // tool-switch on an empty cursor doesn't litter the canvas.
    if (editing_.has_value()) {
        commit_editing();
    }
}

void TextToolHandler::commit_editing() {
    if (!editing_.has_value()) {
        return;
    }
    // The buffer is a null-terminated C-string from ImGui; build
    // the std::string from it.
    std::string content{editing_->buffer};
    auto p = noted::domain::tool::text_primitive_from(
        editing_->position_x, editing_->position_y, std::move(content), editing_->options);
    if (!p.is_empty()) {
        texts_.push_back(std::move(p));
    }
    editing_.reset();
}

void TextToolHandler::cancel_editing() noexcept {
    editing_.reset();
}

}  // namespace noted::app::input
