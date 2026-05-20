#include "noted/ui/widget/brush_options.hpp"

#include <imgui.h>

namespace noted::ui::widget {

namespace {

void draw_pen(noted::domain::tool::PenOptions& opt) {
    // Size range. SliderFloat clamps the value to its bounds; the
    // domain-side `brush_from_pen` clamps again at the data boundary,
    // so a user dragging past the slider's edge can't poison the
    // stroke buffer.
    ImGui::TextDisabled("Pen");
    ImGui::Spacing();
    ImGui::SliderFloat("Min radius", &opt.min_radius_px, 1.0F, 64.0F, "%.1f px");
    ImGui::SliderFloat("Max radius", &opt.max_radius_px, 1.0F, 128.0F, "%.1f px");
    ImGui::SliderFloat(
        "Pressure curve", &opt.alpha_gamma, 0.2F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);

    ImGui::Spacing();
    float colour[4] = {opt.r, opt.g, opt.b, opt.a};
    if (ImGui::ColorEdit4(
            "Colour", colour, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float)) {
        opt.r = colour[0];
        opt.g = colour[1];
        opt.b = colour[2];
        opt.a = colour[3];
    }
}

void draw_eraser(noted::domain::tool::EraserOptions& opt) {
    ImGui::TextDisabled("Eraser");
    ImGui::Spacing();
    ImGui::SliderFloat("Min radius", &opt.min_radius_px, 1.0F, 128.0F, "%.1f px");
    ImGui::SliderFloat("Max radius", &opt.max_radius_px, 1.0F, 256.0F, "%.1f px");
    ImGui::SliderFloat(
        "Pressure curve", &opt.alpha_gamma, 0.2F, 4.0F, "%.2f", ImGuiSliderFlags_Logarithmic);
}

void draw_placeholder(const char* tool_name) {
    ImGui::TextDisabled("%s", tool_name);
    ImGui::Spacing();
    ImGui::TextWrapped("(no options yet — coming in a later phase)");
}

void draw_text(noted::domain::tool::TextOptions& opt) {
    ImGui::TextDisabled("Text");
    ImGui::Spacing();

    ImGui::SliderFloat(
        "Font size", &opt.font_size_px, 8.0F, 200.0F, "%.0f px", ImGuiSliderFlags_Logarithmic);

    ImGui::Spacing();
    float colour[4] = {opt.r, opt.g, opt.b, opt.a};
    if (ImGui::ColorEdit4(
            "Text colour", colour, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float)) {
        opt.r = colour[0];
        opt.g = colour[1];
        opt.b = colour[2];
        opt.a = colour[3];
    }
}

void draw_image(noted::domain::tool::ImageOptions& opt) {
    ImGui::TextDisabled("Image");
    ImGui::Spacing();

    // Width / height sliders. 8 px floor matches the placeholder's
    // visibility threshold (smaller and the centred label drops
    // out — see `image_overlay`); 4096 px is a soft cap chosen for
    // UX, not engine limits.
    ImGui::SliderFloat(
        "Width", &opt.width_px, 8.0F, 4096.0F, "%.0f px", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat(
        "Height", &opt.height_px, 8.0F, 4096.0F, "%.0f px", ImGuiSliderFlags_Logarithmic);

    ImGui::Spacing();
    float colour[4] = {opt.r, opt.g, opt.b, opt.a};
    if (ImGui::ColorEdit4(
            "Tint", colour, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float)) {
        opt.r = colour[0];
        opt.g = colour[1];
        opt.b = colour[2];
        opt.a = colour[3];
    }

    ImGui::Spacing();
    ImGui::TextDisabled("(file picker + GPU upload land in B.7.b)");
}

void draw_shape(noted::domain::tool::ShapeOptions& opt) {
    ImGui::TextDisabled("Shape");
    ImGui::Spacing();

    // Kind picker — combo over the wire-stable enum. Append-only
    // ordinals (see `shape_drag.hpp`), so the labels' index matches
    // the ordinal exactly.
    using noted::domain::tool::ShapeKind;
    constexpr const char* kKindLabels[] = {"Rectangle", "Ellipse"};
    int kind_idx = static_cast<int>(opt.kind);
    if (ImGui::Combo("Kind", &kind_idx, kKindLabels, IM_ARRAYSIZE(kKindLabels))) {
        opt.kind = static_cast<ShapeKind>(kind_idx);
    }

    ImGui::SliderFloat("Stroke width", &opt.stroke_width_px, 0.5F, 16.0F, "%.1f px");

    ImGui::Spacing();
    float colour[4] = {opt.stroke_r, opt.stroke_g, opt.stroke_b, opt.stroke_a};
    if (ImGui::ColorEdit4(
            "Stroke colour", colour, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float)) {
        opt.stroke_r = colour[0];
        opt.stroke_g = colour[1];
        opt.stroke_b = colour[2];
        opt.stroke_a = colour[3];
    }
}

}  // namespace

void brush_options(noted::domain::tool::ToolState& tools, bool* open) {
    if (open != nullptr && !*open) {
        return;
    }
    ImGui::SetNextWindowSize(ImVec2{260.0F, 0.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Brush options", open)) {
        ImGui::End();
        return;
    }
    using noted::domain::tool::ToolKind;
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
            draw_image(tools.image);
            break;
    }
    ImGui::End();
}

}  // namespace noted::ui::widget
