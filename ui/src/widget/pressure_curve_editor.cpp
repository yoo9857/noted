#include "noted/ui/widget/pressure_curve_editor.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

constexpr float kHandleRadius = 5.0F;
constexpr float kHandleHitRadius = 9.0F;
constexpr int kCurveSegments = 48;

// Drag-target identity so a click outside both handles doesn't
// confuse the active-handle selection. Stored as a function-local
// static so the widget remains stateless from the caller's PoV.
struct DragState {
    int active_handle{-1};  // 0 = h1, 1 = h2, -1 = none
};

[[nodiscard]] auto drag_state() -> DragState& {
    static DragState s{};
    return s;
}

[[nodiscard]] auto clamp01(float v) noexcept -> float {
    return std::clamp(v, 0.0F, 1.0F);
}

}  // namespace

auto pressure_curve_editor(noted::stroke::PressureCurve& curve, float size_px) -> bool {
    bool changed = false;
    const float size = std::max(80.0F, size_px);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area_min = origin;
    const ImVec2 area_max(origin.x + size, origin.y + size);

    ImGui::InvisibleButton("##pressure_curve", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    const bool mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const ImVec2 mouse = ImGui::GetMousePos();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- Background grid ----------------------------------------------
    constexpr ImU32 kBg = IM_COL32(0x15, 0x15, 0x18, 0xFF);
    constexpr ImU32 kGrid = IM_COL32(0x2A, 0x2A, 0x2E, 0xFF);
    constexpr ImU32 kBorder = IM_COL32(0x40, 0x40, 0x48, 0xFF);
    dl->AddRectFilled(area_min, area_max, kBg, 4.0F);
    for (int i = 1; i < 4; ++i) {
        const float f = static_cast<float>(i) * 0.25F;
        dl->AddLine(ImVec2(area_min.x + f * size, area_min.y),
                    ImVec2(area_min.x + f * size, area_max.y),
                    kGrid,
                    1.0F);
        dl->AddLine(ImVec2(area_min.x, area_min.y + f * size),
                    ImVec2(area_max.x, area_min.y + f * size),
                    kGrid,
                    1.0F);
    }
    // Diagonal reference (linear pressure mapping).
    dl->AddLine(ImVec2(area_min.x, area_max.y),
                ImVec2(area_max.x, area_min.y),
                IM_COL32(0x40, 0x55, 0x70, 0x80),
                1.0F);
    dl->AddRect(area_min, area_max, kBorder, 4.0F, 0, 1.0F);

    // ---- Curve sampling -----------------------------------------------
    // Local helper: map curve-space (x, y in [0,1]) to screen-space,
    // accounting for the y-down convention of ImGui's draw list (y=0
    // sits at the TOP of the widget, but we want pressure=0 to sit
    // at the BOTTOM so the curve reads as "more pressure → more
    // alpha = rises up to the right").
    const auto to_screen = [&](float x, float y) -> ImVec2 {
        return ImVec2(area_min.x + clamp01(x) * size, area_max.y - clamp01(y) * size);
    };

    constexpr ImU32 kCurveCol = IM_COL32(0x6B, 0xA8, 0xF5, 0xFF);
    ImVec2 prev = to_screen(0.0F, 0.0F);
    for (int i = 1; i <= kCurveSegments; ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(kCurveSegments);
        const float y = curve.evaluate(x);
        const ImVec2 cur = to_screen(x, y);
        dl->AddLine(prev, cur, kCurveCol, 2.0F);
        prev = cur;
    }

    // ---- Handle drag interaction -------------------------------------
    auto& ds = drag_state();
    const ImVec2 h1_screen = to_screen(curve.h1_x, curve.h1_y);
    const ImVec2 h2_screen = to_screen(curve.h2_x, curve.h2_y);

    const auto dist = [](const ImVec2& a, const ImVec2& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    };

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Pick the handle whose screen distance is closest, if within
        // the hit radius. Bias toward h1 on a tie — the user can
        // always grab the other by moving away first.
        const float d1 = dist(mouse, h1_screen);
        const float d2 = dist(mouse, h2_screen);
        if (d1 <= kHandleHitRadius && d1 <= d2) {
            ds.active_handle = 0;
        } else if (d2 <= kHandleHitRadius) {
            ds.active_handle = 1;
        }
    }
    if (!mouse_down) {
        ds.active_handle = -1;
    }

    // Constraint: keep h1.x ≤ h2.x so the curve stays monotonic-x.
    // Allow y to be anywhere in [0, 1] — that's what makes ease-in
    // / ease-out / S-shape achievable.
    auto apply_drag = [&](int idx) {
        const float raw_x = (mouse.x - area_min.x) / size;
        const float raw_y = (area_max.y - mouse.y) / size;
        const float nx = clamp01(raw_x);
        const float ny = clamp01(raw_y);
        if (idx == 0) {
            const float new_x = std::min(nx, curve.h2_x - 0.02F);
            curve.h1_x = std::max(0.0F, new_x);
            curve.h1_y = ny;
        } else if (idx == 1) {
            const float new_x = std::max(nx, curve.h1_x + 0.02F);
            curve.h2_x = std::min(1.0F, new_x);
            curve.h2_y = ny;
        }
        changed = true;
    };

    if (ds.active_handle >= 0 && mouse_down) {
        apply_drag(ds.active_handle);
    }

    // ---- Handles (drawn after curve so they sit on top) --------------
    constexpr ImU32 kHandleFill = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
    constexpr ImU32 kHandleStroke = IM_COL32(0x2F, 0x4A, 0x70, 0xFF);
    constexpr ImU32 kHandleActive = IM_COL32(0xFF, 0xC8, 0x60, 0xFF);
    const ImU32 h1_fill = (ds.active_handle == 0) ? kHandleActive : kHandleFill;
    const ImU32 h2_fill = (ds.active_handle == 1) ? kHandleActive : kHandleFill;
    // Tangent lines from endpoints to handles — gives the visual cue
    // that the handle "pulls" the curve toward it.
    const ImVec2 p0 = to_screen(0.0F, 0.0F);
    const ImVec2 p3 = to_screen(1.0F, 1.0F);
    constexpr ImU32 kTangent = IM_COL32(0x40, 0x60, 0x88, 0xC0);
    dl->AddLine(p0, h1_screen, kTangent, 1.0F);
    dl->AddLine(p3, h2_screen, kTangent, 1.0F);
    dl->AddCircleFilled(h1_screen, kHandleRadius, h1_fill, 14);
    dl->AddCircle(h1_screen, kHandleRadius + 0.5F, kHandleStroke, 14, 1.5F);
    dl->AddCircleFilled(h2_screen, kHandleRadius, h2_fill, 14);
    dl->AddCircle(h2_screen, kHandleRadius + 0.5F, kHandleStroke, 14, 1.5F);

    return changed;
}

}  // namespace noted::ui::widget
