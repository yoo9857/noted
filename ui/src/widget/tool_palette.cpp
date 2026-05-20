#include "noted/ui/widget/tool_palette.hpp"

#include <array>
#include <string>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

// All tools, in display order. Order matches `ToolKind` ordinals so
// the index ↔ enum mapping is direct.
constexpr std::array<noted::domain::tool::ToolKind, noted::domain::tool::kToolCount> kAllTools{{
    noted::domain::tool::ToolKind::pen,
    noted::domain::tool::ToolKind::eraser,
    noted::domain::tool::ToolKind::select,
    noted::domain::tool::ToolKind::shape,
    noted::domain::tool::ToolKind::text,
    noted::domain::tool::ToolKind::image,
}};

}  // namespace

auto tool_palette(noted::domain::tool::ToolKind active, bool* open) -> ToolPaletteResult {
    ToolPaletteResult out{};
    if (open != nullptr && !*open) {
        return out;
    }

    ImGui::SetNextWindowSize(ImVec2{120.0F, 0.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Tools", open)) {
        ImGui::End();
        return out;
    }

    const auto avail = ImGui::GetContentRegionAvail().x;
    const auto& style = ImGui::GetStyle();
    const ImVec4 active_color = style.Colors[ImGuiCol_ButtonActive];

    for (const auto tool : kAllTools) {
        ImGui::PushID(static_cast<int>(tool));
        const bool is_active = (tool == active);
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Button, active_color);
        }
        const std::string label{noted::domain::tool::label(tool)};
        if (ImGui::Button(label.c_str(), ImVec2{avail, 0.0F})) {
            out.switch_request = tool;
        }
        if (is_active) {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }

    ImGui::End();
    return out;
}

}  // namespace noted::ui::widget
