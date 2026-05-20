#include "noted/domain/tool/selection_drag.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace noted::domain::tool {

namespace {

// Round-toward-nearest then cast to int. Manual rather than relying
// on `std::lround` so NaN / inf inputs map to 0 instead of
// implementation-defined behaviour — a runaway pointer event must
// not produce a garbage rect.
[[nodiscard]] auto safe_round_to_int(double v) noexcept -> std::int32_t {
    if (std::isnan(v) || std::isinf(v)) {
        return 0;
    }
    return static_cast<std::int32_t>(std::llround(v));
}

}  // namespace

auto rect_from_drag(double x1, double y1, double x2, double y2) noexcept -> SelectionRect {
    // Sub-pixel drags collapse to empty so a stationary click doesn't
    // commit a selection. Measuring before rounding: a 0.4 px drag
    // between two integer cells would otherwise round to a 1 px rect
    // which is wrong (the user didn't drag).
    const double dx = std::abs(x2 - x1);
    const double dy = std::abs(y2 - y1);
    if (std::isnan(dx) || std::isnan(dy) || dx < 1.0 || dy < 1.0) {
        return SelectionRect{};
    }
    const std::int32_t left = safe_round_to_int(std::min(x1, x2));
    const std::int32_t top = safe_round_to_int(std::min(y1, y2));
    const std::int32_t right = safe_round_to_int(std::max(x1, x2));
    const std::int32_t bottom = safe_round_to_int(std::max(y1, y2));
    const std::int32_t w = std::max(0, right - left);
    const std::int32_t h = std::max(0, bottom - top);
    return SelectionRect{.x = left, .y = top, .width = w, .height = h};
}

void apply_drag(Selection& sel, SelectionRect r, SelectionDragMode mode) {
    if (r.is_empty()) {
        return;
    }
    switch (mode) {
        case SelectionDragMode::replace:
            sel.clear();
            sel.add_rect(r);
            break;
        case SelectionDragMode::add:
            sel.add_rect(r);
            break;
        case SelectionDragMode::subtract:
            sel.subtract_rect(r);
            break;
        case SelectionDragMode::intersect:
            sel.intersect_rect(r);
            break;
    }
}

auto drag_mode_from_modifiers(bool shift, bool alt) noexcept -> SelectionDragMode {
    if (shift && alt) {
        return SelectionDragMode::intersect;
    }
    if (shift) {
        return SelectionDragMode::add;
    }
    if (alt) {
        return SelectionDragMode::subtract;
    }
    return SelectionDragMode::replace;
}

}  // namespace noted::domain::tool
