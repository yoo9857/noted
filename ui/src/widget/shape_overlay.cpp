#include "noted/ui/widget/shape_overlay.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto pack_color(float r, float g, float b, float a) noexcept -> ImU32 {
    const auto clamp01 = [](float v) -> float { return std::clamp(v, 0.0F, 1.0F); };
    const auto cr = static_cast<int>(clamp01(r) * 255.0F);
    const auto cg = static_cast<int>(clamp01(g) * 255.0F);
    const auto cb = static_cast<int>(clamp01(b) * 255.0F);
    const auto ca = static_cast<int>(clamp01(a) * 255.0F);
    return IM_COL32(cr, cg, cb, ca);
}

void draw_one(ImDrawList* dl,
              noted::domain::tool::ShapeKind kind,
              float sx0,
              float sy0,
              float sx1,
              float sy1,
              ImU32 colour,
              float stroke_px) {
    if (sx1 < sx0) {
        std::swap(sx0, sx1);
    }
    if (sy1 < sy0) {
        std::swap(sy0, sy1);
    }
    if (sx1 - sx0 < 1.0F || sy1 - sy0 < 1.0F) {
        return;  // sub-pixel — skip
    }
    using noted::domain::tool::ShapeKind;
    switch (kind) {
        case ShapeKind::rectangle:
            dl->AddRect(ImVec2{sx0, sy0},
                        ImVec2{sx1, sy1},
                        colour,
                        /*rounding=*/0.0F,
                        /*flags=*/0,
                        stroke_px);
            break;
        case ShapeKind::ellipse: {
            const ImVec2 centre{(sx0 + sx1) * 0.5F, (sy0 + sy1) * 0.5F};
            const ImVec2 radius{(sx1 - sx0) * 0.5F, (sy1 - sy0) * 0.5F};
            // num_segments=0 lets ImGui pick adaptive segment count
            // based on the radius — cheap + looks right at any zoom.
            dl->AddEllipse(centre, radius, colour, /*rot=*/0.0F, /*num_segments=*/0, stroke_px);
            break;
        }
    }
}

// In-progress drag preview uses a slightly translucent version of
// the snapshotted stroke colour so the user sees what they're
// committing while distinguishing it from already-committed shapes.
[[nodiscard]] auto preview_alpha_scale(float src_a) noexcept -> float {
    return std::clamp(src_a * 0.75F, 0.0F, 1.0F);
}

}  // namespace

void shape_overlay(const std::vector<noted::domain::tool::ShapePrimitive>& shapes,
                   const std::optional<ShapeDragPreview>& preview,
                   const ShapeCanvasToScreenFn& canvas_to_screen,
                   bool enabled) {
    if (!enabled || !canvas_to_screen) {
        return;
    }
    auto* dl = ImGui::GetBackgroundDrawList();
    if (dl == nullptr) {
        return;
    }

    for (const auto& s : shapes) {
        if (s.is_empty()) {
            continue;
        }
        const auto [sx0, sy0] = canvas_to_screen(s.x0, s.y0);
        const auto [sx1, sy1] = canvas_to_screen(s.x1, s.y1);
        const ImU32 colour = pack_color(s.stroke_r, s.stroke_g, s.stroke_b, s.stroke_a);
        draw_one(dl, s.kind, sx0, sy0, sx1, sy1, colour, s.stroke_width_px);
    }

    if (preview.has_value()) {
        const auto& p = *preview;
        const auto [sx0, sy0] = canvas_to_screen(p.press_canvas_x, p.press_canvas_y);
        const auto [sx1, sy1] = canvas_to_screen(p.current_canvas_x, p.current_canvas_y);
        const ImU32 colour = pack_color(p.options.stroke_r,
                                        p.options.stroke_g,
                                        p.options.stroke_b,
                                        preview_alpha_scale(p.options.stroke_a));
        draw_one(dl,
                 p.options.kind,
                 sx0,
                 sy0,
                 sx1,
                 sy1,
                 colour,
                 std::max(0.5F, p.options.stroke_width_px));
    }
}

}  // namespace noted::ui::widget
