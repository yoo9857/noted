#include "input/text_tool_handler.hpp"

#include <cstring>
#include <utility>

namespace noted::app::input {

TextToolHandler::TextToolHandler(CommandSink sink,
                                 const noted::domain::tool::ToolState& tools) noexcept
    : sink_(std::move(sink)), tools_(tools) {}

auto TextToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::text;
}

void TextToolHandler::on_pressed(double cx, double cy, bool /*shift*/, bool /*alt*/) {
    // Click-while-editing → commit current then start new.
    if (editing_.has_value()) {
        commit_editing();
    }
    noted::domain::tool::TextEditingState st{};
    st.position_x = cx;
    st.position_y = cy;
    st.options = tools_.text;  // snapshot at PRESS
    st.needs_focus = true;
    editing_ = st;
}

void TextToolHandler::on_moved(double /*cx*/, double /*cy*/) {
    // Text is click-to-anchor, not drag.
}

void TextToolHandler::on_released(double /*cx*/, double /*cy*/) {
    // The press anchors; ImGui InputText collects keys between
    // presses.
}

void TextToolHandler::on_deactivated() noexcept {
    // Switching away mid-typing commits whatever's there. Pure-
    // empty buffers are dropped silently by `commit_editing`.
    if (editing_.has_value()) {
        commit_editing();
    }
}

void TextToolHandler::commit_editing() {
    if (!editing_.has_value()) {
        return;
    }
    std::string content{editing_->buffer};
    auto p = noted::domain::tool::text_primitive_from(
        editing_->position_x, editing_->position_y, std::move(content), editing_->options);
    if (!p.is_empty() && sink_) {
        sink_(std::make_unique<noted::domain::AddTextCommand>(std::move(p)));
    }
    editing_.reset();
}

void TextToolHandler::cancel_editing() noexcept {
    editing_.reset();
}

}  // namespace noted::app::input
