#include "noted/ui/widget/layer_panel.hpp"

#include <string>

#include <imgui.h>

#include "noted/domain/layer/layer.hpp"

namespace noted::ui::widget {

namespace {

// Stable text for every BlendMode value. Wire-stable: the table is
// indexed by the enum's integer ordinal (see ADR 0023 for the
// stability commitment).
[[nodiscard]] auto blend_label(noted::domain::BlendMode mode) noexcept -> const char* {
    using BM = noted::domain::BlendMode;
    switch (mode) {
        case BM::normal:
            return "Normal";
        case BM::multiply:
            return "Multiply";
        case BM::screen:
            return "Screen";
        case BM::overlay:
            return "Overlay";
        case BM::soft_light:
            return "Soft Light";
        case BM::hard_light:
            return "Hard Light";
        case BM::color_dodge:
            return "Color Dodge";
        case BM::color_burn:
            return "Color Burn";
        case BM::linear_dodge:
            return "Linear Dodge";
        case BM::linear_burn:
            return "Linear Burn";
        case BM::difference:
            return "Difference";
        case BM::exclusion:
            return "Exclusion";
        case BM::hue:
            return "Hue";
        case BM::saturation:
            return "Saturation";
        case BM::color:
            return "Color";
        case BM::luminosity:
            return "Luminosity";
    }
    return "?";
}

}  // namespace

void layer_panel(noted::domain::LayerGraph& graph, bool* open) {
    if (open != nullptr && !*open) {
        return;
    }
    if (!ImGui::Begin("Layers", open)) {
        ImGui::End();
        return;
    }

    auto order = graph.topological_order();
    if (!order) {
        ImGui::TextUnformatted("(layer graph error)");
        ImGui::TextWrapped("%s", order.error().message.c_str());
        ImGui::End();
        return;
    }

    if (order->empty()) {
        ImGui::TextDisabled("(no layers)");
        ImGui::End();
        return;
    }

    // Reverse the topological order for display so the topmost (last-
    // rendered) layer appears at the top of the panel — matches the
    // mental model every layer-based editor (Photoshop / Krita /
    // Goodnotes) trains.
    if (ImGui::BeginTable("##layers", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("##visible", ImGuiTableColumnFlags_WidthFixed, 22.0F);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Blend", ImGuiTableColumnFlags_WidthFixed, 110.0F);

        for (auto it = order->rbegin(); it != order->rend(); ++it) {
            const auto id = *it;
            const auto* node = graph.find(id);
            if (node == nullptr) {
                continue;
            }
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(id));
            bool visible = node->visible;
            if (ImGui::Checkbox("##v", &visible)) {
                // Direct mutation — no Command yet. Failures here mean
                // the id vanished mid-frame (impossible in our single-
                // threaded UI model); ignore the Result to keep the
                // widget noexcept-shaped.
                (void) graph.set_visible(id, visible);
            }

            ImGui::TableNextColumn();
            if (node->name.empty()) {
                ImGui::TextDisabled("layer %llu", static_cast<unsigned long long>(id));
            } else {
                ImGui::TextUnformatted(node->name.c_str());
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(blend_label(node->blend));
            ImGui::SameLine();
            ImGui::TextDisabled("(%.0f%%)", node->opacity * 100.0F);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

}  // namespace noted::ui::widget
