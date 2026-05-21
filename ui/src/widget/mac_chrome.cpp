#include "noted/ui/widget/mac_chrome.hpp"

#include <algorithm>
#include <string>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

constexpr float kStripHeight = 28.0F;
constexpr float kButtonDiameter = 12.0F;
constexpr float kButtonSpacing = 8.0F;      // between adjacent buttons
constexpr float kButtonLeftMargin = 18.0F;  // from window left edge to first button
constexpr float kButtonTopMargin = 8.0F;    // from strip top to button top

// Mac-canonical traffic-light fills (Sonoma / Sequoia). The hover
// variants are the same hue with a brighter glyph; the pressed
// variant darkens slightly. We pick the values from a colour-picker
// against the live macOS reference rather than guessing.
constexpr ImU32 kRedFill = IM_COL32(0xED, 0x6A, 0x5E, 0xFF);
constexpr ImU32 kRedHover = IM_COL32(0xFF, 0x6E, 0x66, 0xFF);
constexpr ImU32 kYellowFill = IM_COL32(0xF5, 0xBF, 0x4F, 0xFF);
constexpr ImU32 kYellowHover = IM_COL32(0xFF, 0xC8, 0x59, 0xFF);
constexpr ImU32 kGreenFill = IM_COL32(0x62, 0xC5, 0x54, 0xFF);
constexpr ImU32 kGreenHover = IM_COL32(0x70, 0xD5, 0x60, 0xFF);
constexpr ImU32 kGlyph = IM_COL32(0x00, 0x00, 0x00, 0x80);  // semi-opaque black
// Strip background — a soft neutral that reads as "chrome" against
// the page-on-desk grey desk colour without competing with it.
constexpr ImU32 kStripBg = IM_COL32(0x21, 0x21, 0x24, 0xFF);
constexpr ImU32 kStripBorder = IM_COL32(0x00, 0x00, 0x00, 0x40);
constexpr ImU32 kTitleText = IM_COL32(0xD0, 0xD0, 0xD2, 0xFF);

[[nodiscard]] auto draw_traffic_light(ImDrawList* dl,
                                      ImVec2 centre,
                                      float radius,
                                      ImU32 fill,
                                      ImU32 hover_fill,
                                      char glyph,
                                      bool show_glyph) -> bool {
    const auto mouse = ImGui::GetIO().MousePos;
    const float dx = mouse.x - centre.x;
    const float dy = mouse.y - centre.y;
    const bool hovered = (dx * dx + dy * dy) <= (radius * radius);
    dl->AddCircleFilled(centre, radius, hovered ? hover_fill : fill, 24);
    // Subtle 1-px stroke so the buttons read as buttons even against
    // a tinted background. macOS uses ~8 % black on the border.
    dl->AddCircle(centre, radius, IM_COL32(0x00, 0x00, 0x00, 0x33), 24, 1.0F);
    if (show_glyph && hovered) {
        // Single-char monospace glyph centred in the button. Mac
        // uses tiny SF Symbols here (×, –, +); ImGui's default font
        // doesn't have those at small sizes, so we use plain ASCII
        // approximations.
        const char buf[2] = {glyph, '\0'};
        const ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(centre.x - ts.x * 0.5F, centre.y - ts.y * 0.5F), kGlyph, buf);
    }
    return hovered;
}

}  // namespace

auto mac_chrome_height_px() -> float {
    return kStripHeight;
}

auto draw_mac_chrome(std::string_view title, bool is_maximized) -> MacChromeResult {
    MacChromeResult out{};

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float strip_w = vp->Size.x;

    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(strip_w, kStripHeight));
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    if (ImGui::Begin("##mac_chrome", nullptr, kFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 strip_min = ImVec2(0.0F, 0.0F);
        const ImVec2 strip_max = ImVec2(strip_w, kStripHeight);
        dl->AddRectFilled(strip_min, strip_max, kStripBg);
        // 1-px bottom border so the chrome separates cleanly from
        // the menu bar below.
        dl->AddLine(
            ImVec2(0.0F, kStripHeight - 1.0F), ImVec2(strip_w, kStripHeight - 1.0F), kStripBorder);

        const float r = kButtonDiameter * 0.5F;
        const float cy = kStripHeight * 0.5F;
        const float red_cx = kButtonLeftMargin + r;
        const float yellow_cx = red_cx + kButtonDiameter + kButtonSpacing;
        const float green_cx = yellow_cx + kButtonDiameter + kButtonSpacing;

        // We track whether ANY button is hovered so the "show glyph
        // on hover" affordance applies to all three buttons in
        // unison — matches macOS Sonoma's behaviour.
        const auto mouse = ImGui::GetIO().MousePos;
        const auto in_strip = mouse.y >= 0.0F && mouse.y <= kStripHeight;
        const auto hovered_any = in_strip && ((std::abs(mouse.x - red_cx) <= r) ||
                                              (std::abs(mouse.x - yellow_cx) <= r) ||
                                              (std::abs(mouse.x - green_cx) <= r));

        const bool red_hov =
            draw_traffic_light(dl, ImVec2(red_cx, cy), r, kRedFill, kRedHover, 'x', hovered_any);
        const bool yel_hov = draw_traffic_light(
            dl, ImVec2(yellow_cx, cy), r, kYellowFill, kYellowHover, '-', hovered_any);
        const bool grn_hov = draw_traffic_light(dl,
                                                ImVec2(green_cx, cy),
                                                r,
                                                kGreenFill,
                                                kGreenHover,
                                                is_maximized ? '-' : '+',
                                                hovered_any);

        // Click handling — left button up / down inside the circle.
        const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        if (clicked && red_hov) {
            out.close_clicked = true;
        } else if (clicked && yel_hov) {
            out.minimize_clicked = true;
        } else if (clicked && grn_hov) {
            out.maximize_clicked = true;
        } else if (clicked && in_strip && !hovered_any) {
            // Drag region — anywhere on the strip that isn't a button.
            out.drag_started = true;
        }

        // Title text. Centred horizontally; baseline approximately
        // centred vertically (CalcTextSize gives full line height).
        if (!title.empty()) {
            const std::string title_str{title};
            const ImVec2 ts = ImGui::CalcTextSize(title_str.c_str());
            const float tx = (strip_w - ts.x) * 0.5F;
            const float ty = (kStripHeight - ts.y) * 0.5F;
            dl->AddText(ImVec2(tx, ty), kTitleText, title_str.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return out;
}

}  // namespace noted::ui::widget
