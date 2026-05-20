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
            draw_placeholder("Shape");
            break;
        case ToolKind::text:
            draw_placeholder("Text");
            break;
        case ToolKind::image:
            draw_placeholder("Image");
            break;
    }
    ImGui::End();
}

}  // namespace noted::ui::widget
