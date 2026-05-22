#include "noted/ui/widget/color_picker_panel.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

// Real-painter's palette feel:
//   * Round paint-blob swatches with a soft highlight (like wet
//     paint on a wooden palette).
//   * No auto-recent — the `+` button is the one and only way to add
//     a colour, matching the physical-palette mental model: you
//     dollop paint when YOU decide to.
//   * Light wooden tone behind the palette area to suggest a real
//     palette board.

constexpr float kMaxWheelSize = 220.0F;
constexpr float kMinWheelSize = 170.0F;
constexpr float kBlobSize = 38.0F;
constexpr float kBlobGap = 6.0F;
// 5 wells across × 4 rows down = 20 — matches the host's
// `kPaletteSlotCount` and reads as a wide artist's-palette layout.
constexpr int kBlobsPerRow = 5;
// Reference: total palette tray height for kPaletteSlotCount slots
// in a 5-wide grid = 4 rows of 38 px + 3 gaps × 6 px + 2 × 10 px
// tray padding = 190 px. The Color dock proportion in workspace.cpp
// gives ~650 px content at 1000-px window height, so the whole
// palette + wheel + RGBA + hex row fits without scrolling. Blobs
// inside the tray are centred horizontally; left-justified rows
// looked unbalanced against the centred HSV wheel above.

struct DisplayState {
    bool rgb_255_mode{true};
    // When true, the next palette swatch click DELETES that slot
    // instead of applying it. Toggled by the "Erase next" button in
    // the palette header; disarms after a successful deletion or
    // when the user clicks the toggle again. Escape also disarms so
    // a stuck-on state can't trap the user.
    bool erase_armed{false};
};

[[nodiscard]] auto display_state() -> DisplayState& {
    static DisplayState s{};
    return s;
}

struct BlobInteraction {
    bool clicked{false};
    bool right_clicked{false};
};

// Round "paint blob" swatch. A real palette has actual blobs of paint
// you scoop with a brush; circles + a soft top-left highlight read
// that way far better than rounded squares.
[[nodiscard]] auto draw_paint_blob(
    const ImVec4& col, bool empty, float size, int unique_id, bool erase_armed) -> BlobInteraction {
    BlobInteraction r{};
    ImGui::PushID(unique_id);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float radius = size * 0.5F;
    const ImVec2 centre(p.x + radius, p.y + radius);

    ImGui::InvisibleButton("##bl", ImVec2(size, size));
    const bool hovered = ImGui::IsItemHovered();
    r.clicked = !empty && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    r.right_clicked = !empty && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    constexpr int kSegments = 28;

    if (empty) {
        // Empty palette well — a darker ring suggesting a cleaned-out
        // dip. Soft "+" hint on hover invites the user to dollop.
        dl->AddCircleFilled(centre, radius, IM_COL32(0x22, 0x22, 0x26, 0xFF), kSegments);
        dl->AddCircle(centre,
                      radius - 0.5F,
                      IM_COL32(0x3C, 0x3C, 0x42, hovered ? 0xFF : 0x80),
                      kSegments,
                      1.5F);
        const auto glyph_alpha = static_cast<ImU32>(hovered ? 0xA0 : 0x40);
        const float arm = radius * 0.32F;
        dl->AddLine(ImVec2(centre.x - arm, centre.y),
                    ImVec2(centre.x + arm, centre.y),
                    IM_COL32(0xC0, 0xC0, 0xC8, glyph_alpha),
                    1.6F);
        dl->AddLine(ImVec2(centre.x, centre.y - arm),
                    ImVec2(centre.x, centre.y + arm),
                    IM_COL32(0xC0, 0xC0, 0xC8, glyph_alpha),
                    1.6F);
    } else {
        // Filled paint blob — drop shadow (slight offset down-right),
        // body, then a soft top-left specular highlight so it reads
        // as a glossy wet-paint dollop.
        dl->AddCircleFilled(ImVec2(centre.x + 1.0F, centre.y + 2.0F),
                            radius,
                            IM_COL32(0x00, 0x00, 0x00, hovered ? 0x40 : 0x60),
                            kSegments);
        const auto fill = ImGui::ColorConvertFloat4ToU32(col);
        dl->AddCircleFilled(centre, radius, fill, kSegments);
        // Specular highlight — small light circle at ~north-west,
        // fades the swatch into a 3D blob without obscuring its hue.
        const float hl_r = radius * 0.32F;
        const ImVec2 hl_c(centre.x - radius * 0.32F, centre.y - radius * 0.32F);
        dl->AddCircleFilled(hl_c, hl_r, IM_COL32(0xFF, 0xFF, 0xFF, 0x55), kSegments);
        // Subtle rim shadow for depth on the bottom-right.
        dl->AddCircle(centre, radius - 0.5F, IM_COL32(0x00, 0x00, 0x00, 0x60), kSegments, 1.0F);
        if (hovered) {
            // Hover ring — red when armed for erase, otherwise the
            // standard white glow. Bright red is the universal "this
            // click will destroy" affordance.
            const ImU32 ring_col =
                erase_armed ? IM_COL32(0xFF, 0x55, 0x55, 0xE0) : IM_COL32(0xFF, 0xFF, 0xFF, 0xC0);
            dl->AddCircle(centre, radius + 2.5F, ring_col, kSegments, 2.2F);
            if (erase_armed) {
                // Small `x` glyph centred on the blob — unambiguous
                // "delete on click" sign.
                const float arm = radius * 0.36F;
                dl->AddLine(ImVec2(centre.x - arm, centre.y - arm),
                            ImVec2(centre.x + arm, centre.y + arm),
                            IM_COL32(0xFF, 0xFF, 0xFF, 0xE0),
                            2.0F);
                dl->AddLine(ImVec2(centre.x - arm, centre.y + arm),
                            ImVec2(centre.x + arm, centre.y - arm),
                            IM_COL32(0xFF, 0xFF, 0xFF, 0xE0),
                            2.0F);
            }
        }
    }

    ImGui::PopID();
    return r;
}

// Trendy current-colour pill at the top — alpha-checker underneath
// so transparency reads correctly.
void draw_current_pill(const float rgba[4], float width, float height) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 p_end(p.x + width, p.y + height);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    constexpr float kRound = 8.0F;
    constexpr ImU32 kCheckA = IM_COL32(0x30, 0x30, 0x33, 0xFF);
    constexpr ImU32 kCheckB = IM_COL32(0x22, 0x22, 0x25, 0xFF);
    constexpr float kCheck = 6.0F;
    dl->AddRectFilled(p, p_end, kCheckA, kRound);
    dl->PushClipRect(p, p_end, true);
    const int rows = static_cast<int>((p_end.y - p.y) / kCheck) + 1;
    const int cols = static_cast<int>((p_end.x - p.x) / kCheck) + 1;
    for (int r = 0; r < rows; ++r) {
        for (int c = (r % 2); c < cols; c += 2) {
            const ImVec2 cp(p.x + c * kCheck, p.y + r * kCheck);
            dl->AddRectFilled(cp, ImVec2(cp.x + kCheck, cp.y + kCheck), kCheckB);
        }
    }
    dl->PopClipRect();
    const ImVec4 c4(rgba[0], rgba[1], rgba[2], rgba[3]);
    dl->AddRectFilled(p, p_end, ImGui::ColorConvertFloat4ToU32(c4), kRound);
    dl->AddRect(p, p_end, IM_COL32(0xFF, 0xFF, 0xFF, 0x20), kRound, 0, 1.0F);
    ImGui::Dummy(ImVec2(width, height));
}

// Wooden palette tray under the swatch grid. ImGui's
// `AddRectFilledMultiColor` does NOT honour the rounding parameter
// (engine-bar memory: "use stacked rounded fills"), so we use a
// single rounded `AddRectFilled` with a flat warm-brown fill plus a
// subtle inner highlight stroke. The previous gradient version
// produced sharp rectangular corners poking through the rounded
// outline — that's the "뾰족 사각형 튀어나와있음" the user reported.
void draw_palette_background(const ImVec2& min, const ImVec2& max) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    constexpr float kRound = 8.0F;
    constexpr ImU32 kFill = IM_COL32(0x25, 0x22, 0x1E, 0xFF);
    constexpr ImU32 kInnerHilite = IM_COL32(0xFF, 0xFF, 0xFF, 0x10);
    constexpr ImU32 kOutline = IM_COL32(0x00, 0x00, 0x00, 0x80);
    dl->AddRectFilled(min, max, kFill, kRound);
    // 1 px highlight inset along the top edge — gives the tray a
    // bit of dimensionality without a gradient hack.
    dl->AddRect(ImVec2(min.x + 1.0F, min.y + 1.0F),
                ImVec2(max.x - 1.0F, max.y - 1.0F),
                kInnerHilite,
                kRound - 1.0F,
                0,
                1.0F);
    dl->AddRect(min, max, kOutline, kRound, 0, 1.0F);
}

}  // namespace

auto color_picker_panel(float* rgba,
                        const std::array<std::array<float, 4>, kPaletteSlotCount>& palette_colors,
                        float top_offset_px,
                        float panel_width_px) -> ColorPickerResult {
    ColorPickerResult out{};
    if (rgba == nullptr) {
        return out;
    }
    (void) top_offset_px;
    (void) panel_width_px;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0F, 6.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0F, 3.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0F);
    if (!ImGui::Begin("Color")) {
        ImGui::End();
        ImGui::PopStyleVar(5);
        return out;
    }

    auto& display = display_state();
    const float avail_w = ImGui::GetContentRegionAvail().x;

    // ---- Current colour pill (no extra buttons — eyedropper retired
    //      until real screen-pick lands) -----------------------------
    constexpr float kPillHeight = 26.0F;
    draw_current_pill(rgba, avail_w, kPillHeight);

    ImGui::Spacing();

    // ---- Big HSV wheel ------------------------------------------------
    constexpr ImGuiColorEditFlags kPickerFlags =
        ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel |
        ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoTooltip;

    const float wheel_size = std::clamp(avail_w, kMinWheelSize, kMaxWheelSize);
    if (avail_w > wheel_size) {
        ImGui::Indent((avail_w - wheel_size) * 0.5F);
    }
    ImGui::SetNextItemWidth(wheel_size);
    if (ImGui::ColorPicker4("##picker", rgba, kPickerFlags)) {
        out.changed = true;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        out.committed = true;
    }
    if (avail_w > wheel_size) {
        ImGui::Unindent((avail_w - wheel_size) * 0.5F);
    }

    ImGui::Spacing();

    // ---- Aligned 4-column grid: RGBA row + Hex / Range row ----------
    // Both rows share the same column metric so the elements line up
    // vertically as a clean 4-col table:
    //   Row 1: R | G | B | A   (each ~ col_w wide)
    //   Row 2: # hex (spans cols 1+2)   |   Range button (spans cols 3+4)
    // Equal cell width keeps the visual rhythm; a subtle frame-bg
    // tint on every input ties the row together.
    constexpr ImU32 kRedTint = IM_COL32(0xE5, 0x4B, 0x4B, 0xFF);
    constexpr ImU32 kGrnTint = IM_COL32(0x4B, 0xC8, 0x5F, 0xFF);
    constexpr ImU32 kBluTint = IM_COL32(0x4B, 0x82, 0xE5, 0xFF);
    constexpr ImU32 kAlpTint = IM_COL32(0xC0, 0xC0, 0xC8, 0xFF);
    const ImU32 tints[4] = {kRedTint, kGrnTint, kBluTint, kAlpTint};
    const char* labels[4] = {"R", "G", "B", "A"};

    constexpr float kColGap = 6.0F;
    constexpr float kLabelW = 12.0F;
    const float grid_avail = ImGui::GetContentRegionAvail().x;
    const float col_w = (grid_avail - 3.0F * kColGap) / 4.0F;
    const float input_w = std::max(28.0F, col_w - kLabelW);

    // Subtle dark frame tint for all numeric inputs on these two
    // rows. PushStyleColor stays active until popped at the end of
    // the section so every input on both rows shares the same chip
    // styling.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13F, 0.13F, 0.15F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.17F, 0.17F, 0.20F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.21F, 0.21F, 0.25F, 1.0F));

    for (int ch = 0; ch < 4; ++ch) {
        ImGui::PushID(ch);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(tints[ch]));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(labels[ch]);
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0F, 2.0F);
        ImGui::SetNextItemWidth(input_w);
        if (display.rgb_255_mode && ch < 3) {
            int v255 = static_cast<int>(rgba[ch] * 255.0F + 0.5F);
            if (ImGui::DragInt("##v", &v255, 0.5F, 0, 255, "%d")) {
                rgba[ch] = static_cast<float>(std::clamp(v255, 0, 255)) / 255.0F;
                out.changed = true;
            }
        } else {
            if (ImGui::DragFloat("##v", &rgba[ch], 0.005F, 0.0F, 1.0F, "%.2f")) {
                out.changed = true;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            out.committed = true;
        }
        ImGui::PopID();
        if (ch < 3) {
            ImGui::SameLine(0.0F, kColGap);
        }
    }

    // ---- Row 2: Hex (spans 2 cols) + Range button (spans 2 cols) -----
    char hex_buf[8] = {};
    std::snprintf(hex_buf,
                  sizeof(hex_buf),
                  "%02X%02X%02X",
                  static_cast<int>(rgba[0] * 255.0F + 0.5F) & 0xFF,
                  static_cast<int>(rgba[1] * 255.0F + 0.5F) & 0xFF,
                  static_cast<int>(rgba[2] * 255.0F + 0.5F) & 0xFF);

    // Each half row spans 2 col cells + 1 inter-cell gap, matching the
    // column boundary of the RGBA row above. Hex carries the same
    // `#` chip + frame tint as the channel inputs for visual parity.
    const float half_w = 2.0F * col_w + kColGap;
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55F, 0.55F, 0.60F, 1.0F));
    ImGui::TextUnformatted("#");
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0F, 2.0F);
    const float hex_input_w = std::max(40.0F, half_w - kLabelW);
    ImGui::SetNextItemWidth(hex_input_w);
    if (ImGui::InputText(
            "##hex",
            hex_buf,
            sizeof(hex_buf),
            ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_AutoSelectAll)) {
        unsigned int v = 0;
        if (std::sscanf(hex_buf, "%x", &v) == 1) {
            rgba[0] = static_cast<float>((v >> 16) & 0xFF) / 255.0F;
            rgba[1] = static_cast<float>((v >> 8) & 0xFF) / 255.0F;
            rgba[2] = static_cast<float>(v & 0xFF) / 255.0F;
            out.changed = true;
            out.committed = true;
        }
    }
    ImGui::SameLine(0.0F, kColGap);
    const char* range_label = display.rgb_255_mode ? "Range: 0-255" : "Range: 0.0-1";
    if (ImGui::Button(range_label, ImVec2(half_w, 0.0F))) {
        display.rgb_255_mode = !display.rgb_255_mode;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("RGB value display range");
    }

    ImGui::PopStyleColor(3);  // FrameBg / FrameBgHovered / FrameBgActive

    ImGui::Spacing();
    ImGui::Separator();

    // ---- Palette action buttons (label inline on the left) -----------
    // Header label + buttons share one row so the user doesn't see
    // the buttons "drifting down" past a vertical gap. Buttons SHARE
    // the remaining width equally — earlier the fixed 110-px width
    // overflowed the panel when the user shrank the Color dock
    // below ~280 px, pushing the buttons past the right edge.
    constexpr float kBtnGap = 6.0F;
    constexpr float kMinBtnW = 50.0F;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70F, 0.65F, 0.55F, 1.0F));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Palette");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    const float remain = ImGui::GetContentRegionAvail().x;
    const float btn_w = std::max(kMinBtnW, (remain - kBtnGap) * 0.5F);

    if (ImGui::Button("+ Add", ImVec2(btn_w, 0.0F))) {
        out.palette_add_requested = true;
        // Adding while armed-to-erase would be confusing — disarm.
        display.erase_armed = false;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Save the current colour to the first empty well");
    }
    ImGui::SameLine(0.0F, kBtnGap);

    // Erase toggle. Highlighted (red-tinted) when armed so the user
    // sees the mode change at a glance; the next swatch click acts
    // as a delete and the mode disarms.
    if (display.erase_armed) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70F, 0.22F, 0.22F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85F, 0.32F, 0.32F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60F, 0.18F, 0.18F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
    }
    const char* erase_label = display.erase_armed ? "x Cancel" : "x Erase";
    if (ImGui::Button(erase_label, ImVec2(btn_w, 0.0F))) {
        display.erase_armed = !display.erase_armed;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(display.erase_armed
                              ? "Cancel — click again to leave erase mode without removing"
                              : "Arm erase — the next palette well you click will be cleared");
    }
    if (display.erase_armed) {
        ImGui::PopStyleColor(4);
    }

    // Escape always disarms — quick out for the user who armed by
    // accident or wants to bail mid-action. Pressed in the picker's
    // own keyboard scope; the rest of the app doesn't consume Esc
    // here (top toolbar uses Ctrl chords).
    if (display.erase_armed && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        display.erase_armed = false;
    }

    ImGui::Spacing();

    // ---- Wooden palette tray ----------------------------------------
    // A subtle warm-grey gradient panel suggesting a real wooden palette.
    // We draw it BEHIND the swatch grid by capturing the cursor pos,
    // letting the swatches lay out normally, then back-filling.
    const ImVec2 tray_pad(8.0F, 10.0F);
    const ImVec2 tray_min = ImGui::GetCursorScreenPos();
    const float tray_inner_w = ImGui::GetContentRegionAvail().x - 2.0F * tray_pad.x;
    // Compute height ahead of the layout so we can paint the wood
    // first (cleaner than transparent overlap).
    const int row_count =
        (static_cast<int>(palette_colors.size()) + kBlobsPerRow - 1) / kBlobsPerRow;
    const float tray_inner_h = row_count * kBlobSize + std::max(0, row_count - 1) * kBlobGap;
    const ImVec2 tray_max(tray_min.x + tray_inner_w + 2.0F * tray_pad.x,
                          tray_min.y + tray_inner_h + 2.0F * tray_pad.y);
    draw_palette_background(tray_min, tray_max);

    // Pixel-deterministic grid placement. ImGui's SameLine/auto-
    // wrap path computes Y from the previous line's max item height
    // + ItemSpacing.y — close to a grid but with subtle drift if a
    // blob's bbox ever reports a different height than expected
    // (e.g. when hover effects shift line padding). Computing each
    // blob's screen position from the grid origin nails them to
    // integer-pixel coordinates so columns + rows always line up.
    const float grid_w = kBlobsPerRow * kBlobSize + (kBlobsPerRow - 1) * kBlobGap;
    const float centre_offset = std::max(0.0F, (tray_inner_w - grid_w) * 0.5F);
    const ImVec2 grid_origin(tray_min.x + tray_pad.x + centre_offset, tray_min.y + tray_pad.y);
    const int total_rows =
        static_cast<int>((palette_colors.size() + static_cast<std::size_t>(kBlobsPerRow) - 1U) /
                         static_cast<std::size_t>(kBlobsPerRow));

    for (std::size_t i = 0; i < palette_colors.size(); ++i) {
        const int row = static_cast<int>(i) / kBlobsPerRow;
        const int col = static_cast<int>(i) % kBlobsPerRow;
        const ImVec2 pos(grid_origin.x + col * (kBlobSize + kBlobGap),
                         grid_origin.y + row * (kBlobSize + kBlobGap));
        ImGui::SetCursorScreenPos(pos);

        const auto& c = palette_colors[i];
        const bool empty = c[3] <= 0.0F;
        const ImVec4 col_v(c[0], c[1], c[2], c[3]);
        const auto sw =
            draw_paint_blob(col_v, empty, kBlobSize, static_cast<int>(i), display.erase_armed);
        if (sw.clicked) {
            if (display.erase_armed) {
                out.palette_delete_index = static_cast<int>(i);
                display.erase_armed = false;
            } else {
                rgba[0] = c[0];
                rgba[1] = c[1];
                rgba[2] = c[2];
                rgba[3] = c[3];
                out.changed = true;
                out.committed = true;
            }
        }
        if (sw.right_clicked) {
            out.palette_delete_index = static_cast<int>(i);
        }
        if (ImGui::IsItemHovered() && !empty) {
            if (display.erase_armed) {
                ImGui::SetTooltip("Click to remove this swatch (#%02X%02X%02X)",
                                  static_cast<int>(c[0] * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(c[1] * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(c[2] * 255.0F + 0.5F) & 0xFF);
            } else {
                ImGui::SetTooltip("#%02X%02X%02X · Click to use · Right-click to remove",
                                  static_cast<int>(c[0] * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(c[1] * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(c[2] * 255.0F + 0.5F) & 0xFF);
            }
        } else if (ImGui::IsItemHovered() && empty) {
            ImGui::SetTooltip("Empty slot — use '+ Add' to dollop paint here");
        }
    }

    // Advance the cursor past the grid so subsequent panel content
    // (if any) lands below the tray rather than overlapping it.
    const float grid_h = total_rows * kBlobSize + std::max(0, total_rows - 1) * kBlobGap;
    ImGui::SetCursorScreenPos(ImVec2(tray_min.x, tray_min.y + grid_h + 2.0F * tray_pad.y));

    ImGui::End();
    ImGui::PopStyleVar(5);
    return out;
}

}  // namespace noted::ui::widget
