#include "noted/ui/widget/selection_overlay.hpp"

#include <algorithm>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

// Colours chosen so the outline reads against both light and dark
// themes. The outer fill is a low-alpha tint; the border is opaque.
// Marching-ants animation lands in a follow-up — for B.4 a static
// dashed border + tinted fill is the MVP.
constexpr ImU32 kBorderCommitted = IM_COL32(80, 160, 255, 220);
constexpr ImU32 kFillCommitted = IM_COL32(80, 160, 255, 40);
constexpr ImU32 kBorderPreview = IM_COL32(255, 200, 60, 255);
constexpr ImU32 kFillPreview = IM_COL32(255, 200, 60, 35);
constexpr float kBorderWidth = 1.5F;

void draw_rect_screen(
    ImDrawList* dl, float x0, float y0, float x1, float y1, ImU32 fill, ImU32 border) {
    if (x1 < x0) {
        std::swap(x0, x1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
    }
    if (x1 - x0 < 1.0F || y1 - y0 < 1.0F) {
        // Sub-pixel rect — skip rather than draw a zero-area
        // artifact. The committed selection's rect_from_drag also
        // filters these out, but the in-progress drag preview can
        // still hit this path on the first frame.
        return;
    }
    dl->AddRectFilled(ImVec2{x0, y0}, ImVec2{x1, y1}, fill);
    dl->AddRect(
        ImVec2{x0, y0}, ImVec2{x1, y1}, border, /*rounding=*/0.0F, /*flags=*/0, kBorderWidth);
}

}  // namespace

void selection_overlay(const noted::domain::Selection& selection,
                       const std::optional<SelectionDragPreview>& preview,
                       const CanvasToScreenFn& canvas_to_screen,
                       bool enabled) {
    if (!enabled || !canvas_to_screen) {
        return;
    }
    auto* dl = ImGui::GetBackgroundDrawList();
    if (dl == nullptr) {
        return;
    }

    for (const auto& r : selection.rects()) {
        if (r.is_empty()) {
            continue;
        }
        const auto [sx0, sy0] =
            canvas_to_screen(static_cast<double>(r.x), static_cast<double>(r.y));
        const auto [sx1, sy1] =
            canvas_to_screen(static_cast<double>(r.right()), static_cast<double>(r.bottom()));
        draw_rect_screen(dl, sx0, sy0, sx1, sy1, kFillCommitted, kBorderCommitted);
    }

    if (preview.has_value()) {
        const auto& p = *preview;
        const auto [sx0, sy0] = canvas_to_screen(p.press_canvas_x, p.press_canvas_y);
        const auto [sx1, sy1] = canvas_to_screen(p.current_canvas_x, p.current_canvas_y);
        draw_rect_screen(dl, sx0, sy0, sx1, sy1, kFillPreview, kBorderPreview);
    }
}

}  // namespace noted::ui::widget
