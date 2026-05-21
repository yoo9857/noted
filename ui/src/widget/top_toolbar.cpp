#include "noted/ui/widget/top_toolbar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

// Base button geometry. The dock-style hover magnification expands
// individual buttons up to `kButtonH * kHoverScale` while keeping
// neighbours pushed outward proportionally — the macOS Dock effect.
constexpr float kButtonW = 44.0F;
constexpr float kButtonH = 40.0F;
constexpr float kButtonGap = 6.0F;
constexpr float kGroupGap = 12.0F;
constexpr float kPillPaddingX = 12.0F;
constexpr float kPillPaddingY = 6.0F;
constexpr float kPillRounding = 18.0F;
constexpr float kHoverScale = 1.18F;      // peak scale under the cursor
constexpr float kHoverFalloffPx = 60.0F;  // distance to fall back to 1.0×

// Colour palette — leans into the system-accent + soft-grey
// vocabulary that reads as "modern Mac" without being a pixel
// copy of any specific app.
constexpr ImU32 kPillFillTop = IM_COL32(0x2D, 0x2D, 0x33, 0xF2);
constexpr ImU32 kPillFillBottom = IM_COL32(0x1A, 0x1A, 0x1E, 0xF2);
constexpr ImU32 kPillHighlight = IM_COL32(0xFF, 0xFF, 0xFF, 0x14);
constexpr ImU32 kPillBorder = IM_COL32(0x00, 0x00, 0x00, 0x80);
constexpr ImU32 kAccentFill = IM_COL32(0x0A, 0x84, 0xFF, 0xFF);
constexpr ImU32 kAccentDot = IM_COL32(0x0A, 0x84, 0xFF, 0xFF);
constexpr ImU32 kHoverFill = IM_COL32(0x4A, 0x4A, 0x52, 0xFF);
constexpr ImU32 kSeparator = IM_COL32(0xFF, 0xFF, 0xFF, 0x18);
constexpr ImU32 kIconStroke = IM_COL32(0xE5, 0xE5, 0xEA, 0xFF);
constexpr ImU32 kIconStrokeMuted = IM_COL32(0x8E, 0x8E, 0x93, 0xFF);
constexpr ImU32 kIconStrokeDisabled = IM_COL32(0x55, 0x55, 0x5A, 0xFF);

using ToolKind = noted::domain::tool::ToolKind;

// ---------- Procedural icon drawing ---------------------------------------
// Every tool icon is rendered via ImDrawList primitives so the toolbar
// never depends on a font's glyph availability — the "?" we hit with
// Malgun + Segoe UI Symbol can't happen with line / circle / rect
// primitives.

void draw_pen_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Diagonal pen body from upper-right to lower-left, with a
    // pointed nib at the lower-left tip. Two parallel strokes give
    // the body its "pen barrel" feel.
    const float r = s * 0.32F;
    const ImVec2 tip(c.x - r, c.y + r);
    const ImVec2 base(c.x + r * 0.85F, c.y - r * 0.85F);
    dl->AddLine(tip, base, col, 2.0F);
    const ImVec2 n_off(r * 0.18F, r * 0.18F);
    dl->AddLine(ImVec2(tip.x + n_off.x, tip.y - n_off.y),
                ImVec2(base.x + n_off.x, base.y - n_off.y),
                col,
                2.0F);
    dl->AddTriangleFilled(tip,
                          ImVec2(tip.x + r * 0.20F, tip.y - r * 0.05F),
                          ImVec2(tip.x + r * 0.05F, tip.y - r * 0.20F),
                          col);
}

void draw_eraser_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Tilted rounded rectangle suggesting a rubber eraser.
    const float w = s * 0.55F;
    const float h = s * 0.30F;
    dl->AddRect(ImVec2(c.x - w, c.y - h), ImVec2(c.x + w, c.y + h), col, 3.0F, 0, 2.0F);
    // Divider line giving the "two-tone" eraser look.
    dl->AddLine(
        ImVec2(c.x - w + s * 0.18F, c.y - h), ImVec2(c.x - w + s * 0.18F, c.y + h), col, 1.5F);
}

void draw_select_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Dashed-border rectangle = the rectangle selection marquee.
    const float r = s * 0.34F;
    constexpr int kDashes = 8;
    const ImVec2 corners[4] = {
        ImVec2(c.x - r, c.y - r),
        ImVec2(c.x + r, c.y - r),
        ImVec2(c.x + r, c.y + r),
        ImVec2(c.x - r, c.y + r),
    };
    for (int i = 0; i < 4; ++i) {
        const ImVec2 a = corners[i];
        const ImVec2 b = corners[(i + 1) % 4];
        for (int d = 0; d < kDashes; d += 2) {
            const float t0 = static_cast<float>(d) / kDashes;
            const float t1 = static_cast<float>(d + 1) / kDashes;
            const ImVec2 p0(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0);
            const ImVec2 p1(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1);
            dl->AddLine(p0, p1, col, 1.5F);
        }
    }
}

void draw_lasso_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Free-form closed curve — distinguishes the lasso from the
    // rect-marquee Select tool. Plot a wobbly tear-drop shape via
    // a 16-point ellipse with a sinusoidal radial perturbation;
    // dashed so it reads as a "selection path" same as marquee.
    constexpr int kSeg = 18;
    const float r = s * 0.32F;
    ImVec2 prev{};
    for (int i = 0; i <= kSeg; ++i) {
        const float t = static_cast<float>(i) / kSeg * 6.2831853F;
        const float wob = 0.85F + 0.15F * std::sin(t * 3.0F);
        const ImVec2 p(c.x + std::cos(t) * r * wob, c.y + std::sin(t) * r * wob);
        if (i > 0 && (i % 2) == 0) {
            dl->AddLine(prev, p, col, 1.5F);
        }
        prev = p;
    }
}

void draw_shape_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Filled rounded square + outlined circle overlapping — reads
    // as "primitive shapes".
    const float a = s * 0.30F;
    dl->AddRectFilled(ImVec2(c.x - a, c.y - a), ImVec2(c.x + a * 0.4F, c.y + a * 0.4F), col, 3.0F);
    dl->AddCircle(ImVec2(c.x + a * 0.3F, c.y + a * 0.3F), a * 0.55F, col, 16, 2.0F);
}

void draw_text_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Serif-style "T" — horizontal top bar with two short serifs and
    // a vertical stem.
    const float w = s * 0.50F;
    const float h = s * 0.55F;
    dl->AddLine(ImVec2(c.x - w, c.y - h * 0.5F), ImVec2(c.x + w, c.y - h * 0.5F), col, 2.5F);
    dl->AddLine(ImVec2(c.x, c.y - h * 0.5F), ImVec2(c.x, c.y + h * 0.5F), col, 2.5F);
}

void draw_image_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Frame + a small sun + a triangle "mountain" — the universal
    // picture icon.
    const float r = s * 0.36F;
    const ImVec2 tl(c.x - r, c.y - r * 0.8F);
    const ImVec2 br(c.x + r, c.y + r * 0.8F);
    dl->AddRect(tl, br, col, 3.0F, 0, 2.0F);
    // Sun (top-left).
    dl->AddCircleFilled(ImVec2(c.x - r * 0.4F, c.y - r * 0.3F), r * 0.16F, col, 8);
    // Mountain (bottom).
    dl->AddLine(ImVec2(c.x - r * 0.7F, c.y + r * 0.6F), ImVec2(c.x, c.y - r * 0.1F), col, 2.0F);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.1F), ImVec2(c.x + r * 0.8F, c.y + r * 0.6F), col, 2.0F);
}

void draw_undo_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Counter-clockwise arc with an arrowhead at the start (top).
    const float r = s * 0.32F;
    constexpr int kSeg = 24;
    constexpr float kStart = -0.4F * 3.14159F;  // tip
    constexpr float kEnd = 1.4F * 3.14159F;     // ~285°
    ImVec2 prev{};
    for (int i = 0; i <= kSeg; ++i) {
        const float t = kStart + (kEnd - kStart) * static_cast<float>(i) / kSeg;
        const ImVec2 p(c.x + std::cos(t) * r, c.y + std::sin(t) * r);
        if (i > 0) {
            dl->AddLine(prev, p, col, 2.0F);
        }
        prev = p;
    }
    // Arrow head at the start.
    const float ah = s * 0.14F;
    const ImVec2 tip(c.x + std::cos(kStart) * r, c.y + std::sin(kStart) * r);
    dl->AddLine(tip, ImVec2(tip.x - ah, tip.y - ah * 0.4F), col, 2.0F);
    dl->AddLine(tip, ImVec2(tip.x - ah * 0.4F, tip.y + ah), col, 2.0F);
}

void draw_redo_icon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    // Clockwise arc — mirror of undo.
    const float r = s * 0.32F;
    constexpr int kSeg = 24;
    constexpr float kStart = -0.6F * 3.14159F;
    constexpr float kEnd = -2.4F * 3.14159F;
    ImVec2 prev{};
    for (int i = 0; i <= kSeg; ++i) {
        const float t = kStart + (kEnd - kStart) * static_cast<float>(i) / kSeg;
        const ImVec2 p(c.x + std::cos(t) * r, c.y + std::sin(t) * r);
        if (i > 0) {
            dl->AddLine(prev, p, col, 2.0F);
        }
        prev = p;
    }
    const float ah = s * 0.14F;
    const ImVec2 tip(c.x + std::cos(kStart) * r, c.y + std::sin(kStart) * r);
    dl->AddLine(tip, ImVec2(tip.x + ah, tip.y - ah * 0.4F), col, 2.0F);
    dl->AddLine(tip, ImVec2(tip.x + ah * 0.4F, tip.y + ah), col, 2.0F);
}

using IconDrawFn = void (*)(ImDrawList*, ImVec2, float, ImU32);

struct ToolButton {
    ToolKind kind;
    IconDrawFn icon;
    std::string_view tooltip;
};

constexpr std::array<ToolButton, 7> kTools{{
    {ToolKind::pen, &draw_pen_icon, "Pen (B)"},
    {ToolKind::eraser, &draw_eraser_icon, "Eraser (E)"},
    {ToolKind::select, &draw_select_icon, "Rectangle select (V)"},
    {ToolKind::lasso, &draw_lasso_icon, "Lasso (L)"},
    {ToolKind::shape, &draw_shape_icon, "Shape (U)"},
    {ToolKind::text, &draw_text_icon, "Text (T)"},
    {ToolKind::image, &draw_image_icon, "Image (I)"},
}};

// ---------- Pill + button rendering ---------------------------------------

void draw_pill_shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding) {
    // Five-layer drop shadow for the "floating dock" feel.
    for (int i = 1; i <= 5; ++i) {
        const float off = static_cast<float>(i) * 1.5F;
        const ImU32 col = IM_COL32(0, 0, 0, 0x28 - i * 0x06);
        dl->AddRectFilled(ImVec2(min.x - off, min.y + off * 0.4F),
                          ImVec2(max.x + off, max.y + off + 2.0F),
                          col,
                          rounding + off);
    }
}

void draw_pill_body(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding) {
    // ImGui's `AddRectFilledMultiColor` does NOT support rounding —
    // it always fills the whole bounding box, so the gradient
    // square corners visibly poked past the rounded outline. Use
    // a single-colour rounded fill instead, then layer a second
    // rounded rect over the top half to fake a soft gradient. Both
    // calls honour rounding, so the corners stay clean.
    dl->AddRectFilled(min, max, kPillFillBottom, rounding);
    // Top half lighter — gives the embossed "Dock" feel without
    // breaking the rounded silhouette.
    const ImVec2 top_max(max.x, (min.y + max.y) * 0.5F + 1.0F);
    dl->AddRectFilled(min, top_max, kPillFillTop, rounding, ImDrawFlags_RoundCornersTop);
    dl->AddRect(min, max, kPillBorder, rounding, 0, 1.0F);
    dl->AddLine(ImVec2(min.x + rounding * 0.6F, min.y + 1.0F),
                ImVec2(max.x - rounding * 0.6F, min.y + 1.0F),
                kPillHighlight,
                1.0F);
}

[[nodiscard]] auto hover_scale_for(float button_centre_x,
                                   float mouse_x,
                                   bool strip_hovered) -> float {
    if (!strip_hovered) {
        return 1.0F;
    }
    const float d = std::abs(button_centre_x - mouse_x);
    if (d >= kHoverFalloffPx) {
        return 1.0F;
    }
    // Smooth cosine falloff — neighbours scale up too but less, the
    // signature Mac Dock magnification curve.
    const float t = 1.0F - (d / kHoverFalloffPx);
    return 1.0F + (kHoverScale - 1.0F) * t * t;
}

[[nodiscard]] auto draw_button(ImDrawList* dl,
                               ImVec2 centre,
                               float w,
                               float h,
                               IconDrawFn icon,
                               std::string_view tooltip,
                               bool active,
                               bool enabled,
                               bool hovered_now) -> bool {
    const ImVec2 b_min(centre.x - w * 0.5F, centre.y - h * 0.5F);
    const ImVec2 b_max(centre.x + w * 0.5F, centre.y + h * 0.5F);

    ImU32 fill = IM_COL32(0, 0, 0, 0);
    if (active) {
        fill = kAccentFill;
    } else if (hovered_now) {
        fill = kHoverFill;
    }
    if (fill != 0) {
        dl->AddRectFilled(b_min, b_max, fill, 8.0F);
    }

    const ImU32 stroke = !enabled ? kIconStrokeDisabled : kIconStroke;
    icon(dl, centre, std::min(w, h), stroke);

    // Active-tool dot below the button — Mac Dock's "running app"
    // indicator.
    if (active) {
        dl->AddCircleFilled(ImVec2(centre.x, b_max.y + 5.0F), 2.0F, kAccentDot, 8);
    }

    if (hovered_now && !tooltip.empty()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
        ImGui::EndTooltip();
    }
    return hovered_now && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

}  // namespace

auto top_toolbar(ToolKind active,
                 const TopToolbarStatus& status,
                 float top_offset_px) -> TopToolbarResult {
    TopToolbarResult out{};

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float viewport_w = vp->Size.x;

    // Compute pill at NORMAL scale; magnification adjusts visual sizes
    // but the pill layout stays fixed so the dock doesn't jump around
    // as the user moves the cursor.
    const float tools_w = static_cast<float>(kTools.size()) * kButtonW +
                          static_cast<float>(kTools.size() - 1) * kButtonGap;
    constexpr float kHistoryButtons = 2.0F;
    const float history_w = kHistoryButtons * kButtonW + (kHistoryButtons - 1.0F) * kButtonGap;
    const float pill_inner_w = tools_w + kGroupGap + 1.0F + kGroupGap + history_w;
    const float pill_w = pill_inner_w + 2.0F * kPillPaddingX;
    // Pill height accommodates the peak-scaled button so it never
    // overflows the top.
    const float pill_h = kButtonH * kHoverScale + 2.0F * kPillPaddingY;

    const float pill_x = (viewport_w - pill_w) * 0.5F;
    const float pill_y = top_offset_px;
    const ImVec2 pill_min(pill_x, pill_y);
    const ImVec2 pill_max(pill_x + pill_w, pill_y + pill_h);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    // Window slightly larger than the pill to accommodate the shadow
    // halo + the magnification overshoot on the bottom edge.
    constexpr float kHaloPx = 12.0F;
    ImGui::SetNextWindowPos(ImVec2(pill_min.x - kHaloPx, pill_min.y - kHaloPx));
    ImGui::SetNextWindowSize(ImVec2(pill_w + 2.0F * kHaloPx, pill_h + 2.0F * kHaloPx));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (ImGui::Begin("##top_toolbar_overlay", nullptr, kFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        draw_pill_shadow(dl, pill_min, pill_max, kPillRounding);
        draw_pill_body(dl, pill_min, pill_max, kPillRounding);

        // Mouse hover detection — magnification applies only when
        // the cursor sits over the pill strip itself.
        const auto mouse = ImGui::GetIO().MousePos;
        const bool strip_hovered = mouse.x >= pill_min.x && mouse.x <= pill_max.x &&
                                   mouse.y >= pill_min.y && mouse.y <= pill_max.y;

        const float row_cy = (pill_min.y + pill_max.y) * 0.5F;
        float x = pill_min.x + kPillPaddingX + kButtonW * 0.5F;

        // Tool buttons.
        for (const auto& t : kTools) {
            const float scale = hover_scale_for(x, mouse.x, strip_hovered);
            const float w = kButtonW * scale;
            const float h = kButtonH * scale;
            const ImVec2 centre(x, row_cy);
            const bool hovered_now =
                strip_hovered && mouse.x >= centre.x - w * 0.5F && mouse.x <= centre.x + w * 0.5F &&
                mouse.y >= centre.y - h * 0.5F && mouse.y <= centre.y + h * 0.5F;
            if (draw_button(
                    dl, centre, w, h, t.icon, t.tooltip, t.kind == active, true, hovered_now)) {
                out.switch_request = t.kind;
            }
            x += kButtonW + kButtonGap;
        }

        // Group separator.
        const float sep_x = x - kButtonW * 0.5F - kButtonGap * 0.5F + kGroupGap;
        dl->AddLine(ImVec2(sep_x, pill_min.y + kPillPaddingY + 6.0F),
                    ImVec2(sep_x, pill_max.y - kPillPaddingY - 6.0F),
                    kSeparator,
                    1.0F);
        x = sep_x + kGroupGap + kButtonW * 0.5F;

        // Undo / Redo.
        for (int hi = 0; hi < 2; ++hi) {
            const float scale = hover_scale_for(x, mouse.x, strip_hovered);
            const float w = kButtonW * scale;
            const float h = kButtonH * scale;
            const ImVec2 centre(x, row_cy);
            const bool hovered_now =
                strip_hovered && mouse.x >= centre.x - w * 0.5F && mouse.x <= centre.x + w * 0.5F &&
                mouse.y >= centre.y - h * 0.5F && mouse.y <= centre.y + h * 0.5F;
            const auto icon = hi == 0 ? &draw_undo_icon : &draw_redo_icon;
            const auto tooltip =
                hi == 0 ? std::string_view{"Undo (Ctrl+Z)"} : std::string_view{"Redo (Ctrl+Y)"};
            const bool can = hi == 0 ? status.can_undo : status.can_redo;
            if (draw_button(dl, centre, w, h, icon, tooltip, false, can, hovered_now && can)) {
                if (hi == 0) {
                    out.undo_clicked = true;
                } else {
                    out.redo_clicked = true;
                }
            }
            x += kButtonW + kButtonGap;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return out;
}

}  // namespace noted::ui::widget
