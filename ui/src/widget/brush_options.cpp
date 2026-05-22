#include "noted/ui/widget/brush_options.hpp"

#include <filesystem>
#include <string>

#include <imgui.h>

#include "noted/domain/document/image_asset_registry.hpp"
#include "noted/domain/tool/brush_library.hpp"
#include "noted/ui/widget/brush_library_panel.hpp"

namespace noted::ui::widget {

namespace {

// Compact label-on-left helper — narrow leading column for the field
// name, full-width slider on the right. Mirrors the Photoshop /
// Procreate convention where every tool-option row is ≤ 22 px tall.
void compact_label(const char* label, float label_w) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_w);
}

void draw_pen(noted::domain::tool::PenOptions& opt) {
    constexpr float kLabelW = 70.0F;
    const float row_w = ImGui::GetContentRegionAvail().x - kLabelW - 4.0F;

    ImGui::TextDisabled("Pen");

    compact_label("Min size", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##minr", &opt.min_radius_px, 1.0F, 64.0F, "%.1f");

    compact_label("Max size", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##maxr", &opt.max_radius_px, 1.0F, 128.0F, "%.1f");

    compact_label("Pressure", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##ag", &opt.alpha_gamma, 0.2F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);

    compact_label("Opacity", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##op", &opt.a, 0.0F, 1.0F, "%.2f");

    compact_label("Stabilize", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##stab", &opt.stabilizer, 0.0F, 0.95F, "%.2f");
}

void draw_eraser(noted::domain::tool::EraserOptions& opt) {
    constexpr float kLabelW = 70.0F;
    const float row_w = ImGui::GetContentRegionAvail().x - kLabelW - 4.0F;

    ImGui::TextDisabled("Eraser");

    compact_label("Min size", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##minr", &opt.min_radius_px, 1.0F, 128.0F, "%.1f");

    compact_label("Max size", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##maxr", &opt.max_radius_px, 1.0F, 256.0F, "%.1f");

    compact_label("Pressure", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##ag", &opt.alpha_gamma, 0.2F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
}

void draw_placeholder(const char* tool_name) {
    ImGui::TextDisabled("%s", tool_name);
    ImGui::TextWrapped("(no options yet)");
}

void draw_text(noted::domain::tool::TextOptions& opt) {
    constexpr float kLabelW = 70.0F;
    const float row_w = ImGui::GetContentRegionAvail().x - kLabelW - 4.0F;

    ImGui::TextDisabled("Text");

    compact_label("Font size", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat(
        "##fs", &opt.font_size_px, 8.0F, 200.0F, "%.0f", ImGuiSliderFlags_Logarithmic);

    compact_label("Color", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    float colour[4] = {opt.r, opt.g, opt.b, opt.a};
    if (ImGui::ColorEdit4(
            "##tc", colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        opt.r = colour[0];
        opt.g = colour[1];
        opt.b = colour[2];
        opt.a = colour[3];
    }
}

[[nodiscard]] auto draw_image(noted::domain::tool::ImageOptions& opt,
                              const noted::domain::ImageAssetRegistry& image_assets) -> bool {
    constexpr float kLabelW = 70.0F;
    const float row_w = ImGui::GetContentRegionAvail().x - kLabelW - 4.0F;

    ImGui::TextDisabled("Image");

    compact_label("Width", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##w", &opt.width_px, 8.0F, 4096.0F, "%.0f", ImGuiSliderFlags_Logarithmic);

    compact_label("Height", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##h", &opt.height_px, 8.0F, 4096.0F, "%.0f", ImGuiSliderFlags_Logarithmic);

    compact_label("Tint", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    float colour[4] = {opt.r, opt.g, opt.b, opt.a};
    if (ImGui::ColorEdit4(
            "##ti", colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        opt.r = colour[0];
        opt.g = colour[1];
        opt.b = colour[2];
        opt.a = colour[3];
    }

    const bool clicked = ImGui::Button("Pick image…", ImVec2(-FLT_MIN, 0.0F));
    if (opt.pending_asset_id != noted::domain::invalid_asset_id) {
        if (const auto* asset = image_assets.find(opt.pending_asset_id); asset != nullptr) {
            std::filesystem::path p{asset->source_path};
            std::string label = p.filename().empty() ? asset->source_path : p.filename().string();
            ImGui::TextDisabled(
                "%s · %u×%u", label.c_str(), asset->intrinsic_w_px, asset->intrinsic_h_px);
        } else {
            opt.pending_asset_id = noted::domain::invalid_asset_id;
            ImGui::TextDisabled("(no image)");
        }
    } else {
        ImGui::TextDisabled("(no image)");
    }
    return clicked;
}

void draw_shape(noted::domain::tool::ShapeOptions& opt) {
    constexpr float kLabelW = 70.0F;
    const float row_w = ImGui::GetContentRegionAvail().x - kLabelW - 4.0F;

    ImGui::TextDisabled("Shape");

    using noted::domain::tool::ShapeKind;
    constexpr const char* kKindLabels[] = {"Rectangle", "Ellipse"};
    int kind_idx = static_cast<int>(opt.kind);
    compact_label("Kind", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    if (ImGui::Combo("##k", &kind_idx, kKindLabels, IM_ARRAYSIZE(kKindLabels))) {
        opt.kind = static_cast<ShapeKind>(kind_idx);
    }

    compact_label("Width", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    ImGui::SliderFloat("##sw", &opt.stroke_width_px, 0.5F, 16.0F, "%.1f");

    compact_label("Color", kLabelW);
    ImGui::SetNextItemWidth(row_w);
    float colour[4] = {opt.stroke_r, opt.stroke_g, opt.stroke_b, opt.stroke_a};
    if (ImGui::ColorEdit4(
            "##sc", colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
        opt.stroke_r = colour[0];
        opt.stroke_g = colour[1];
        opt.stroke_b = colour[2];
        opt.stroke_a = colour[3];
    }
}

}  // namespace

auto brush_options(noted::domain::tool::ToolState& tools,
                   const noted::domain::ImageAssetRegistry& image_assets,
                   const noted::domain::tool::BrushLibrary* library,
                   noted::domain::tool::BrushPresetId active_preset_id,
                   bool* open) -> BrushOptionsResult {
    BrushOptionsResult result{};
    if (open != nullptr && !*open) {
        return result;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 4.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0F, 2.0F));
    if (!ImGui::Begin("Brush options", open)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return result;
    }

    using noted::domain::tool::ToolKind;

    // Brush library grid — rendered ABOVE the slider section ONLY
    // when the Pen tool is active (presets are pen-shaped). Drawn
    // inside this Begin/End scope so the two sections share one
    // ImGui window instance — calling Begin twice in a frame on the
    // same window splits it into two stacked floaters, which was
    // the "Brush options UI is scattered everywhere" bug.
    if (library != nullptr && tools.active == ToolKind::pen) {
        const auto la = brush_library_panel(*library, tools.pen, active_preset_id);
        using K = BrushLibraryAction::Kind;
        switch (la.kind) {
            case K::none:
                break;
            case K::apply:
                result.library_action = BrushOptionsResult::LibraryAction::apply;
                result.library_preset_id = la.preset_id;
                break;
            case K::save_current:
                result.library_action = BrushOptionsResult::LibraryAction::save_current;
                result.library_save_name = std::move(la.save_name);
                break;
            case K::remove:
                result.library_action = BrushOptionsResult::LibraryAction::remove;
                result.library_preset_id = la.preset_id;
                break;
        }
        ImGui::Separator();
    }

    switch (tools.active) {
        case ToolKind::pen:
            draw_pen(tools.pen);
            break;
        case ToolKind::eraser:
            draw_eraser(tools.eraser);
            break;
        case ToolKind::select:
            draw_placeholder("Select");
            break;
        case ToolKind::shape:
            draw_shape(tools.shape);
            break;
        case ToolKind::text:
            draw_text(tools.text);
            break;
        case ToolKind::image:
            result.pick_image_requested = draw_image(tools.image, image_assets);
            break;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return result;
}

}  // namespace noted::ui::widget
