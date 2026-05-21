#include "noted/ui/widget/color_picker_panel.hpp"

#include <cstdio>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

constexpr float kPaddingX = 10.0F;
constexpr float kPaddingY = 10.0F;
constexpr float kSwatchSize = 26.0F;
constexpr float kSwatchGap = 4.0F;
constexpr int kSwatchesPerRow = 4;

constexpr ImU32 kPanelFillTop = IM_COL32(0x2D, 0x2D, 0x33, 0xF2);
constexpr ImU32 kPanelFillBottom = IM_COL32(0x1A, 0x1A, 0x1E, 0xF2);
constexpr ImU32 kPanelHighlight = IM_COL32(0xFF, 0xFF, 0xFF, 0x14);
constexpr ImU32 kPanelBorder = IM_COL32(0x00, 0x00, 0x00, 0x80);

void draw_panel_chrome(ImDrawList* dl, ImVec2 min, ImVec2 max) {
    constexpr float kRounding = 12.0F;
    for (int i = 1; i <= 5; ++i) {
        const float off = static_cast<float>(i) * 1.5F;
        const ImU32 col = IM_COL32(0, 0, 0, 0x28 - i * 0x06);
        dl->AddRectFilled(ImVec2(min.x - off, min.y + off * 0.4F),
                          ImVec2(max.x + off, max.y + off + 2.0F),
                          col,
                          kRounding + off);
    }
    dl->AddRectFilledMultiColor(
        min, max, kPanelFillTop, kPanelFillTop, kPanelFillBottom, kPanelFillBottom);
    dl->AddRect(min, max, kPanelBorder, kRounding, 0, 1.0F);
    dl->AddLine(ImVec2(min.x + kRounding * 0.6F, min.y + 1.0F),
                ImVec2(max.x - kRounding * 0.6F, min.y + 1.0F),
                kPanelHighlight,
                1.0F);
}

}  // namespace

auto color_picker_panel(float* rgba,
                        const std::array<std::array<float, 4>, 16>& palette_colors,
                        float top_offset_px,
                        float panel_width_px) -> ColorPickerResult {
    ColorPickerResult out{};
    if (rgba == nullptr) {
        return out;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float viewport_w = vp->Size.x;

    // First-time position; user can drag the panel anywhere afterwards
    // and ImGui's ini storage remembers it across frames.
    constexpr float kRightGutter = 12.0F;
    const float panel_x = viewport_w - panel_width_px - kRightGutter;
    const float panel_y = top_offset_px;

    // Plain ImGui Begin — same UX as brush_options:
    //   - draggable (no NoMove)
    //   - collapsible (no NoCollapse)
    //   - resizable (no NoResize)
    //   - the user's position + size + collapsed state persist
    //     across frames via ImGui's ini storage.
    ImGui::SetNextWindowPos(ImVec2(panel_x, panel_y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(panel_width_px, 0.0F), ImGuiCond_FirstUseEver);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0F, 8.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0F);
    if (!ImGui::Begin("Color")) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return out;
    }
    {
        // HSV hue wheel with inner SV triangle — the standard OC8-
        // style picker. Triangle rotates to point at the selected
        // hue on the ring; that's expected behaviour, not a bug.
        constexpr ImGuiColorEditFlags kPickerFlags =
            ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel |
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel |
            ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float;
        ImGui::PushItemWidth(panel_width_px - 2.0F * kPaddingX);
        if (ImGui::ColorPicker4("##picker", rgba, kPickerFlags)) {
            out.changed = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            out.committed = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        // RGB / A sliders — live-update only. Per ColorPickerResult
        // contract, they set `changed` but NEVER `committed` so a
        // single user gesture doesn't push multiple palette entries.
        const float slider_w = panel_width_px - 2.0F * kPaddingX - 28.0F;
        ImGui::PushItemWidth(slider_w);
        const char* labels[3] = {"R", "G", "B"};
        for (int ch = 0; ch < 3; ++ch) {
            ImGui::TextUnformatted(labels[ch]);
            ImGui::SameLine();
            ImGui::PushID(ch);
            if (ImGui::SliderFloat("##rgb", &rgba[ch], 0.0F, 1.0F, "%.2f")) {
                out.changed = true;
            }
            ImGui::PopID();
        }
        ImGui::TextUnformatted("A");
        ImGui::SameLine();
        if (ImGui::SliderFloat("##a", &rgba[3], 0.0F, 1.0F, "%.2f")) {
            out.changed = true;
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();

        // Hex input — round-trips RGB through the standard six-digit
        // hex notation. Commits on Enter.
        char hex_buf[8] = {};
        std::snprintf(hex_buf,
                      sizeof(hex_buf),
                      "%02X%02X%02X",
                      static_cast<int>(rgba[0] * 255.0F + 0.5F) & 0xFF,
                      static_cast<int>(rgba[1] * 255.0F + 0.5F) & 0xFF,
                      static_cast<int>(rgba[2] * 255.0F + 0.5F) & 0xFF);
        ImGui::TextUnformatted("HEX");
        ImGui::SameLine();
        ImGui::PushItemWidth(panel_width_px - 2.0F * kPaddingX - 36.0F);
        if (ImGui::InputText("##hex",
                             hex_buf,
                             sizeof(hex_buf),
                             ImGuiInputTextFlags_CharsHexadecimal |
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                 ImGuiInputTextFlags_CharsUppercase)) {
            unsigned int v = 0;
            if (std::sscanf(hex_buf, "%x", &v) == 1) {
                rgba[0] = static_cast<float>((v >> 16) & 0xFF) / 255.0F;
                rgba[1] = static_cast<float>((v >> 8) & 0xFF) / 255.0F;
                rgba[2] = static_cast<float>(v & 0xFF) / 255.0F;
                out.changed = true;
                out.committed = true;
            }
        }
        ImGui::PopItemWidth();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Palette header with explicit + button.
        ImGui::TextUnformatted("Palette");
        ImGui::SameLine(panel_width_px - 2.0F * kPaddingX - 24.0F);
        if (ImGui::Button("+", ImVec2(20.0F, 20.0F))) {
            out.palette_add_requested = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save current colour to palette");
        }

        // Palette grid — 4×4. Left-click applies the swatch; right-
        // click deletes it (sets that slot's alpha to 0, marking it
        // as the placeholder).
        for (std::size_t i = 0; i < palette_colors.size(); ++i) {
            const auto& c = palette_colors[i];
            const bool empty = c[3] <= 0.0F;
            ImGui::PushID(static_cast<int>(i));
            if (empty) {
                // Placeholder swatch — clearly disabled-looking,
                // can't apply, can't delete (already empty).
                ImGui::ColorButton("##empty",
                                   ImVec4(0.18F, 0.18F, 0.20F, 1.0F),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder,
                                   ImVec2(kSwatchSize, kSwatchSize));
            } else {
                const ImVec4 col(c[0], c[1], c[2], c[3]);
                if (ImGui::ColorButton("##swatch",
                                       col,
                                       ImGuiColorEditFlags_NoTooltip,
                                       ImVec2(kSwatchSize, kSwatchSize))) {
                    rgba[0] = c[0];
                    rgba[1] = c[1];
                    rgba[2] = c[2];
                    rgba[3] = c[3];
                    out.changed = true;
                    out.committed = true;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    out.palette_delete_index = static_cast<int>(i);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Click: apply  ·  Right-click: remove");
                }
            }
            ImGui::PopID();
            if ((i + 1) % kSwatchesPerRow != 0) {
                ImGui::SameLine(0.0F, kSwatchGap);
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Reset", ImVec2(-1.0F, 0.0F))) {
            rgba[0] = 0.0F;
            rgba[1] = 0.0F;
            rgba[2] = 0.0F;
            rgba[3] = 1.0F;
            out.changed = true;
            out.committed = true;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return out;
}

}  // namespace noted::ui::widget
