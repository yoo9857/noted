#include "input/image_tool_handler.hpp"

namespace noted::app::input {

ImageToolHandler::ImageToolHandler(std::vector<noted::domain::tool::ImagePrimitive>& images,
                                   const noted::domain::tool::ToolState& tools) noexcept
    : images_(images), tools_(tools) {}

auto ImageToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::image;
}

void ImageToolHandler::on_pressed(double cx, double cy, bool /*shift*/, bool /*alt*/) {
    // Snapshot the current `ImageOptions` at press; image_primitive_from
    // clamps degenerate sizes to a 1 px floor.
    auto p = noted::domain::tool::image_primitive_from(cx, cy, tools_.image);
    if (!p.is_degenerate()) {
        images_.push_back(p);
    }
}

void ImageToolHandler::on_moved(double /*cx*/, double /*cy*/) {
    // Click-to-place — no drag state.
}

void ImageToolHandler::on_released(double /*cx*/, double /*cy*/) {
    // Click-to-place — the press already committed.
}

void ImageToolHandler::on_deactivated() noexcept {
    // No in-flight state.
}

}  // namespace noted::app::input
