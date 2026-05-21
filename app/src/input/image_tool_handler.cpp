#include "input/image_tool_handler.hpp"

#include <utility>

namespace noted::app::input {

ImageToolHandler::ImageToolHandler(CommandSink sink,
                                   const noted::domain::tool::ToolState& tools) noexcept
    : sink_(std::move(sink)), tools_(tools) {}

auto ImageToolHandler::handled_kind() const noexcept -> noted::domain::tool::ToolKind {
    return noted::domain::tool::ToolKind::image;
}

void ImageToolHandler::on_pressed(double cx, double cy, bool /*shift*/, bool /*alt*/) {
    // Snapshot the currently-picked asset id at press time. The
    // resulting `ImagePrimitive` carries `asset_id == invalid_asset_id`
    // when the user hasn't picked a file yet — `image_overlay` falls
    // back to the placeholder rendering in that case.
    auto p = noted::domain::tool::image_primitive_from(
        cx, cy, tools_.image, tools_.image.pending_asset_id);
    if (!p.is_degenerate() && sink_) {
        sink_(std::make_unique<noted::domain::AddImageCommand>(std::move(p)));
    }
}

void ImageToolHandler::on_moved(double /*cx*/, double /*cy*/) {
    // Click-to-place — no drag.
}

void ImageToolHandler::on_released(double /*cx*/, double /*cy*/) {
    // Click-to-place — the press already committed.
}

void ImageToolHandler::on_deactivated() noexcept {
    // No in-flight state.
}

}  // namespace noted::app::input
