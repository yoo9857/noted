#include "noted/ui/widget/brush_library_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "noted/domain/tool/brush_library.hpp"
#include "noted/domain/tool/options.hpp"

namespace noted::ui::widget {

namespace {

// Card dimensions. Tuned so 4 cards fit in a ~360 px panel without
// the labels truncating; smaller cards lose the preview readability,
// bigger cards push the slider section off-screen.
constexpr float kCardW = 78.0F;
constexpr float kCardH = 88.0F;
constexpr float kCardGap = 8.0F;
constexpr float kPreviewH = 50.0F;

// Number of stamps along the preview path. More = smoother
// gradient, but adds cost per card. 14 reads cleanly at 70 × 50 px.
constexpr int kPreviewStamps = 14;

[[nodiscard]] auto lerp(float a, float b, float t) noexcept -> float {
    return a + (b - a) * t;
}

// Draw a single brush-stamp preview inside the rect [min, max].
// Lays stamps along a gentle S-curve so the preview reads as a
// proper stroke sample rather than a static blob.
void draw_preview_stroke(ImDrawList* dl,
                         const ImVec2& min,
                         const ImVec2& max,
                         const noted::domain::tool::BrushPreset& p,
                         bool active) {
    // Reserve a small margin so the largest stamp doesn't clip into
    // the card chrome.
    const float pad = 6.0F;
    const ImVec2 path_min(min.x + pad, min.y + pad);
    const ImVec2 path_max(max.x - pad, max.y - pad);
    const float path_w = path_max.x - path_min.x;
    const float path_h = path_max.y - path_min.y;

    // Bell-curve pressure: 0 → 1 → 0 across the stamp range, so the
    // sample stroke tapers at both ends. Multiplied by `path_h /
    // max_radius_px` so the stamps fit even when the preset's max
    // radius is bigger than the card.
    const float radius_scale = std::min(1.0F, (path_h * 0.45F) / std::max(0.5F, p.max_radius_px));

    // Stamp colour. Alpha gets the preset alpha (so Marker / Airbrush
    // read as semi-transparent) and the per-stamp pressure
    // contribution.
    const float base_r = p.use_preset_color ? p.r : 0.10F;
    const float base_g = p.use_preset_color ? p.g : 0.10F;
    const float base_b = p.use_preset_color ? p.b : 0.12F;

    const float center_y = (path_min.y + path_max.y) * 0.5F;
    const float wave_amp = path_h * 0.22F;
    const float kPi = 3.1415926535F;

    for (int i = 0; i < kPreviewStamps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kPreviewStamps - 1);
        const float x = path_min.x + t * path_w;
        // Smooth S-curve via sin(pi * (2t - 1))/2.
        const float y = center_y + std::sin((2.0F * t - 1.0F) * kPi) * wave_amp * 0.5F;

        // Pressure bell — light tap at edges, full press in the
        // middle. Mirrors how a quick test stroke feels.
        const float p_norm = std::sin(t * kPi);
        const float pressure = std::pow(p_norm, p.alpha_gamma);
        const float radius = lerp(p.min_radius_px, p.max_radius_px, p_norm) * radius_scale;

        // Stamp colour with pressure-multiplied alpha.
        const float alpha = p.a * pressure;
        const auto col = IM_COL32(static_cast<int>(base_r * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(base_g * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(base_b * 255.0F + 0.5F) & 0xFF,
                                  static_cast<int>(alpha * 255.0F + 0.5F) & 0xFF);

        if (p.softness > 0.5F) {
            // Soft-edge brushes get a smaller core + a halo for
            // visual differentiation.
            const auto halo = IM_COL32(static_cast<int>(base_r * 255.0F + 0.5F) & 0xFF,
                                       static_cast<int>(base_g * 255.0F + 0.5F) & 0xFF,
                                       static_cast<int>(base_b * 255.0F + 0.5F) & 0xFF,
                                       static_cast<int>(alpha * 0.4F * 255.0F + 0.5F) & 0xFF);
            dl->AddCircleFilled(ImVec2(x, y), radius * 1.4F, halo, 14);
            dl->AddCircleFilled(ImVec2(x, y), radius * 0.7F, col, 14);
        } else {
            dl->AddCircleFilled(ImVec2(x, y), radius, col, 14);
        }
    }
    (void) active;
}

}  // namespace

auto brush_library_panel(const noted::domain::tool::BrushLibrary& library,
                         const noted::domain::tool::PenOptions& current_pen,
                         noted::domain::tool::BrushPresetId active_preset_id)
    -> BrushLibraryAction {
    BrushLibraryAction action{};

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70F, 0.65F, 0.55F, 1.0F));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Brushes");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::SmallButton("Save current")) {
        action.kind = BrushLibraryAction::Kind::save_current;
        // Default name — host de-duplicates / decorates as needed.
        action.save_name = "My brush";
        // Echo a hint of the current pen so the host can spot a
        // fully-default-vs-tweaked save if it cares.
        (void) current_pen;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Save the current Pen options as a new preset");
    }

    // Card grid. We compute columns from the available width so the
    // section adapts to a narrowed panel (4 cards typical, 3 / 2 / 1
    // on smaller panels).
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, static_cast<int>((avail_w + kCardGap) / (kCardW + kCardGap)));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto presets = library.presets();
    // Capture the grid origin ONCE before the loop. Reading the
    // cursor every iteration meant each `InvisibleButton` advanced
    // the cursor (no SameLine), so the origin drifted downward
    // every card — producing the "buttons scattered everywhere"
    // bug. Pixel-deterministic placement from a fixed origin is
    // the same pattern the palette grid uses.
    const ImVec2 grid_origin = ImGui::GetCursorScreenPos();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const auto& preset = presets[i];
        const bool is_active = (preset.id == active_preset_id);
        const int col = static_cast<int>(i) % cols;
        const int row = static_cast<int>(i) / cols;
        const ImVec2 card_min(grid_origin.x + col * (kCardW + kCardGap),
                              grid_origin.y + row * (kCardH + kCardGap));
        const ImVec2 card_max(card_min.x + kCardW, card_min.y + kCardH);

        ImGui::SetCursorScreenPos(card_min);
        ImGui::PushID(static_cast<int>(preset.id));
        ImGui::InvisibleButton("##card", ImVec2(kCardW, kCardH));
        const bool hovered = ImGui::IsItemHovered();
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool right_clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

        // ---- Card chrome --------------------------------------------------
        constexpr float kRound = 6.0F;
        const ImU32 card_bg = is_active ? IM_COL32(0x2A, 0x3C, 0x55, 0xFF)
                              : hovered ? IM_COL32(0x26, 0x26, 0x2A, 0xFF)
                                        : IM_COL32(0x1C, 0x1C, 0x1F, 0xFF);
        dl->AddRectFilled(card_min, card_max, card_bg, kRound);
        const ImU32 card_border = is_active ? IM_COL32(0x4B, 0x82, 0xE5, 0xFF)
                                  : hovered ? IM_COL32(0x5A, 0x5A, 0x65, 0xFF)
                                            : IM_COL32(0x33, 0x33, 0x38, 0xFF);
        dl->AddRect(card_min, card_max, card_border, kRound, 0, 1.5F);

        // Preview area (top section).
        const ImVec2 prev_min(card_min.x + 4.0F, card_min.y + 4.0F);
        const ImVec2 prev_max(card_max.x - 4.0F, card_min.y + 4.0F + kPreviewH);
        dl->AddRectFilled(prev_min, prev_max, IM_COL32(0x10, 0x10, 0x12, 0xFF), 4.0F);
        draw_preview_stroke(dl, prev_min, prev_max, preset, is_active);

        // Name label (bottom section).
        const ImVec2 label_min(card_min.x + 4.0F, prev_max.y + 2.0F);
        const ImVec2 label_max(card_max.x - 4.0F, card_max.y - 2.0F);
        const ImU32 label_col =
            is_active ? IM_COL32(0xFF, 0xFF, 0xFF, 0xFF) : IM_COL32(0xC0, 0xC0, 0xC8, 0xFF);
        const auto text = preset.name.c_str();
        const auto text_size = ImGui::CalcTextSize(text);
        const float text_x =
            label_min.x + std::max(0.0F, (label_max.x - label_min.x - text_size.x) * 0.5F);
        const float text_y =
            label_min.y + std::max(0.0F, (label_max.y - label_min.y - text_size.y) * 0.5F);
        dl->AddText(ImVec2(text_x, text_y), label_col, text);

        ImGui::PopID();

        // ---- Interaction ------------------------------------------------
        if (clicked) {
            action.kind = BrushLibraryAction::Kind::apply;
            action.preset_id = preset.id;
        }
        if (right_clicked && !library.is_builtin(preset.id)) {
            action.kind = BrushLibraryAction::Kind::remove;
            action.preset_id = preset.id;
        }
        if (hovered) {
            const char* family = noted::domain::tool::label_of(preset.kind);
            const bool builtin = library.is_builtin(preset.id);
            ImGui::SetTooltip(
                "%s · %s · %.0f–%.0f px%s",
                preset.name.c_str(),
                family,
                preset.min_radius_px,
                preset.max_radius_px,
                builtin ? "\n(Built-in)" : "\n(Click to apply · Right-click to remove)");
        }
    }

    // Advance the cursor past the grid so the brush-options sliders
    // land below the cards rather than overlapping them. Reset to
    // the grid origin first because the loop's last
    // InvisibleButton left the cursor somewhere inside the grid;
    // letting Dummy add to that leaks vertical space.
    const int total_rows = (static_cast<int>(presets.size()) + cols - 1) / std::max(1, cols);
    const float grid_h = total_rows * kCardH + std::max(0, total_rows - 1) * kCardGap;
    ImGui::SetCursorScreenPos(grid_origin);
    ImGui::Dummy(ImVec2(0.0F, grid_h));

    return action;
}

}  // namespace noted::ui::widget
