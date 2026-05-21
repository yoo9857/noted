#include "noted/ui/widget/top_toolbar.hpp"

#include <array>
#include <string_view>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

constexpr float kButtonW = 36.0F;
constexpr float kButtonH = 32.0F;
constexpr float kButtonGap = 4.0F;
constexpr float kGroupGap = 10.0F;
constexpr float kPillPaddingX = 8.0F;
constexpr float kPillPaddingY = 4.0F;
constexpr float kPillRounding = 12.0F;
constexpr float kPillShadowBlur = 8.0F;

constexpr ImU32 kPillFill = IM_COL32(0x1F, 0x1F, 0x22, 0xF0);  // slightly translucent
constexpr ImU32 kPillBorder = IM_COL32(0x2F, 0x2F, 0x33, 0xFF);
constexpr ImU32 kPillShadow = IM_COL32(0x00, 0x00, 0x00, 0x55);
constexpr ImU32 kAccentFill = IM_COL32(0x0A, 0x84, 0xFF, 0xFF);
constexpr ImU32 kAccentText = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr ImU32 kIdleText = IM_COL32(0xC8, 0xC8, 0xCC, 0xFF);
constexpr ImU32 kHoverFill = IM_COL32(0x3A, 0x3A, 0x3F, 0xFF);
constexpr ImU32 kDisabledText = IM_COL32(0x60, 0x60, 0x66, 0xFF);
constexpr ImU32 kSeparator = IM_COL32(0x40, 0x40, 0x45, 0xFF);

using ToolKind = noted::domain::tool::ToolKind;

struct ToolButton {
    ToolKind kind;
    std::string_view glyph;
    std::string_view tooltip;
};

// Glyphs use the default ImGui font (ProggyClean / system fallback),
// so we restrict ourselves to ASCII characters that render legibly
// at small sizes. A follow-up loads Lucide / Tabler as an ImGui
// merged icon font for proper monochrome icons.
constexpr std::array<ToolButton, 6> kTools{{
    {ToolKind::pen, "Pen", "Pen (B)"},
    {ToolKind::eraser, "Era", "Eraser (E)"},
    {ToolKind::select, "Sel", "Select (V)"},
    {ToolKind::shape, "Sh", "Shape (U)"},
    {ToolKind::text, "T", "Text (T)"},
    {ToolKind::image, "Img", "Image (I)"},
}};

// Approximate macOS Sonoma drop-shadow by stacking three offset
// translucent rects with increasing offset — cheaper than a real
// Gaussian on ImGui's draw list and reads as "soft glow" at the
// blur radius we want.
void draw_pill_shadow(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding) {
    for (int i = 1; i <= 3; ++i) {
        const float off = static_cast<float>(i);
        const ImU32 col = IM_COL32(0, 0, 0, 0x22 - i * 0x08);
        dl->AddRectFilled(ImVec2(min.x - off, min.y + off),
                          ImVec2(max.x + off, max.y + off + 1.0F),
                          col,
                          rounding + off);
    }
}

[[nodiscard]] auto draw_button(ImDrawList* dl,
                               ImVec2 min,
                               ImVec2 max,
                               std::string_view glyph,
                               std::string_view tooltip,
                               bool active,
                               bool enabled) -> bool {
    const auto mouse = ImGui::GetIO().MousePos;
    const bool hovered =
        enabled && mouse.x >= min.x && mouse.x <= max.x && mouse.y >= min.y && mouse.y <= max.y;

    ImU32 fill = IM_COL32(0, 0, 0, 0);
    if (active) {
        fill = kAccentFill;
    } else if (hovered) {
        fill = kHoverFill;
    }
    if (fill != 0) {
        dl->AddRectFilled(min, max, fill, 6.0F);
    }

    const ImU32 text_col = !enabled ? kDisabledText : (active ? kAccentText : kIdleText);
    const std::string g{glyph};
    const ImVec2 ts = ImGui::CalcTextSize(g.c_str());
    const ImVec2 c =
        ImVec2((min.x + max.x) * 0.5F - ts.x * 0.5F, (min.y + max.y) * 0.5F - ts.y * 0.5F);
    dl->AddText(c, text_col, g.c_str());

    if (hovered && !tooltip.empty()) {
        // ImGui's BeginTooltip handles positioning + shadow itself —
        // simpler than rolling another draw-list path for this.
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip.data(), tooltip.data() + tooltip.size());
        ImGui::EndTooltip();
    }
    return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

}  // namespace

auto top_toolbar(ToolKind active,
                 const TopToolbarStatus& status,
                 float top_offset_px) -> TopToolbarResult {
    TopToolbarResult out{};

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float viewport_w = vp->Size.x;

    const float tools_w = static_cast<float>(kTools.size()) * kButtonW +
                          static_cast<float>(kTools.size() - 1) * kButtonGap;
    constexpr float kHistoryButtons = 2.0F;
    const float history_w = kHistoryButtons * kButtonW + (kHistoryButtons - 1.0F) * kButtonGap;
    const float pill_inner_w = tools_w + kGroupGap + 1.0F + kGroupGap + history_w;
    const float pill_w = pill_inner_w + 2.0F * kPillPaddingX;
    const float pill_h = kButtonH + 2.0F * kPillPaddingY;

    const float pill_x = (viewport_w - pill_w) * 0.5F;
    const float pill_y = top_offset_px;
    const ImVec2 pill_min(pill_x, pill_y);
    const ImVec2 pill_max(pill_x + pill_w, pill_y + pill_h);

    // ImGui window sized to the pill only. Clicks INSIDE the pill
    // are absorbed (so they don't fall through to the canvas as
    // pen presses); clicks OUTSIDE pass through to the canvas
    // naturally because nothing else covers the area.
    // The shadow's slightly-larger footprint isn't a clickable
    // surface — visual only, harmless to overlap the canvas's
    // input area.
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    ImGui::SetNextWindowPos(pill_min);
    ImGui::SetNextWindowSize(ImVec2(pill_w, pill_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (ImGui::Begin("##top_toolbar_overlay", nullptr, kFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        draw_pill_shadow(dl, pill_min, pill_max, kPillRounding);
        dl->AddRectFilled(pill_min, pill_max, kPillFill, kPillRounding);
        dl->AddRect(pill_min, pill_max, kPillBorder, kPillRounding, 0, 1.0F);

        float x = pill_min.x + kPillPaddingX;
        const float y0 = pill_min.y + kPillPaddingY;
        const float y1 = y0 + kButtonH;

        // Tool buttons.
        for (const auto& t : kTools) {
            const ImVec2 b_min(x, y0);
            const ImVec2 b_max(x + kButtonW, y1);
            if (draw_button(dl, b_min, b_max, t.glyph, t.tooltip, t.kind == active, true)) {
                out.switch_request = t.kind;
            }
            x += kButtonW + kButtonGap;
        }

        // Group separator (vertical hairline).
        x += kGroupGap - kButtonGap;
        dl->AddLine(ImVec2(x, y0 + 4.0F), ImVec2(x, y1 - 4.0F), kSeparator, 1.0F);
        x += 1.0F + kGroupGap;

        // Undo / Redo.
        {
            const ImVec2 b_min(x, y0);
            const ImVec2 b_max(x + kButtonW, y1);
            if (draw_button(dl,
                            b_min,
                            b_max,
                            "\xe2\x86\xb6" /*↶*/,
                            "Undo (Ctrl+Z)",
                            false,
                            status.can_undo)) {
                out.undo_clicked = true;
            }
            x += kButtonW + kButtonGap;
        }
        {
            const ImVec2 b_min(x, y0);
            const ImVec2 b_max(x + kButtonW, y1);
            if (draw_button(dl,
                            b_min,
                            b_max,
                            "\xe2\x86\xb7" /*↷*/,
                            "Redo (Ctrl+Y)",
                            false,
                            status.can_redo)) {
                out.redo_clicked = true;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return out;
}

}  // namespace noted::ui::widget
