#include "noted/ui/widget/layer_panel.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <string>

#include <imgui.h>

#include "noted/domain/document/document.hpp"
#include "noted/domain/layer/canvas_layer_stack.hpp"
#include "noted/domain/layer/layer.hpp"  // BlendMode

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto next_layer_name(const noted::domain::CanvasLayerStack& stack) -> std::string {
    int max_ord = 0;
    for (const auto& layer : stack.layers()) {
        constexpr std::string_view kPrefix = "Layer ";
        if (layer.name.size() <= kPrefix.size() ||
            layer.name.compare(0, kPrefix.size(), kPrefix) != 0) {
            continue;
        }
        try {
            const auto ord = std::stoi(layer.name.substr(kPrefix.size()));
            if (ord > max_ord) {
                max_ord = ord;
            }
        } catch (const std::invalid_argument&) {
            // Non-numeric tail — skip.
        } catch (const std::out_of_range&) {
            // Pathological 20-digit number — skip rather than crash.
        }
    }
    return "Layer " + std::to_string(max_ord + 1);
}

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

constexpr std::array<noted::domain::BlendMode, 16> kAllBlendModes{
    noted::domain::BlendMode::normal,       noted::domain::BlendMode::multiply,
    noted::domain::BlendMode::screen,       noted::domain::BlendMode::overlay,
    noted::domain::BlendMode::soft_light,   noted::domain::BlendMode::hard_light,
    noted::domain::BlendMode::color_dodge,  noted::domain::BlendMode::color_burn,
    noted::domain::BlendMode::linear_dodge, noted::domain::BlendMode::linear_burn,
    noted::domain::BlendMode::difference,   noted::domain::BlendMode::exclusion,
    noted::domain::BlendMode::hue,          noted::domain::BlendMode::saturation,
    noted::domain::BlendMode::color,        noted::domain::BlendMode::luminosity,
};

struct RenameEdit {
    noted::LayerId target{noted::invalid_layer_id};
    std::array<char, 96> buf{};
};

[[nodiscard]] auto rename_state() -> RenameEdit& {
    static RenameEdit edit{};
    return edit;
}

}  // namespace

auto layer_panel(noted::domain::Document& doc, bool* open) -> LayerPanelAction {
    LayerPanelAction action{};
    if (open != nullptr && !*open) {
        return action;
    }
    if (!ImGui::Begin("Layers", open)) {
        ImGui::End();
        return action;
    }

    const auto& stack = doc.canvas_layers();
    const auto active_id = doc.active_layer();
    auto& renaming = rename_state();

    // ---- Top toolbar — structural actions on the active layer -----------
    const bool have_active = active_id != noted::invalid_layer_id;
    std::size_t active_idx = stack.size();
    if (have_active) {
        for (std::size_t i = 0; i < stack.size(); ++i) {
            if (stack.layers()[i].id == active_id) {
                active_idx = i;
                break;
            }
        }
    }
    const bool can_move_up = have_active && active_idx != stack.size() && active_idx < stack.size() - 1U;
    const bool can_move_down = have_active && active_idx != stack.size() && active_idx > 0;
    const bool can_remove = stack.size() > 1U && have_active;
    const bool can_duplicate = have_active && active_idx != stack.size();

    if (ImGui::SmallButton("+")) {
        action.kind = LayerPanelAction::Kind::add;
        action.add_name = next_layer_name(stack);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Add layer");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_duplicate);
    if (ImGui::SmallButton("dup")) {
        action.kind = LayerPanelAction::Kind::duplicate;
        action.index = active_idx;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && can_duplicate) {
        ImGui::SetTooltip("Duplicate layer (Ctrl+J)");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_move_up);
    if (ImGui::ArrowButton("##move_up", ImGuiDir_Up)) {
        action.kind = LayerPanelAction::Kind::move_up;
        action.index = active_idx;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && can_move_up) {
        ImGui::SetTooltip("Move layer up");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_move_down);
    if (ImGui::ArrowButton("##move_down", ImGuiDir_Down)) {
        action.kind = LayerPanelAction::Kind::move_down;
        action.index = active_idx;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && can_move_down) {
        ImGui::SetTooltip("Move layer down");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_remove);
    if (ImGui::SmallButton("x")) {
        action.kind = LayerPanelAction::Kind::remove;
        action.index = active_idx;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && can_remove) {
        ImGui::SetTooltip("Remove layer");
    }
    ImGui::Separator();

    // ---- Per-layer rows -------------------------------------------------
    if (stack.empty()) {
        ImGui::TextDisabled("(no layers yet — '+' above)");
        ImGui::End();
        return action;
    }

    // Display top → bottom. The stack stores bottom-up; reverse for
    // display so the topmost layer reads first.
    const auto& layers = stack.layers();
    for (std::size_t i = layers.size(); i-- > 0;) {
        const auto& layer = layers[i];
        const bool is_active = (layer.id == active_id);

        ImGui::PushID(static_cast<int>(layer.id));

        // Active-row tint.
        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.20F, 0.36F, 0.60F, 0.30F));
            ImGui::BeginChild("##row", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        } else {
            ImGui::BeginChild("##row", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
        }

        // Row 1: visibility eye + lock + name (clickable for active).
        const char* eye_glyph = layer.visible ? "o" : "-";
        if (ImGui::SmallButton(eye_glyph)) {
            if (auto r = doc.set_layer_visible(layer.id, !layer.visible); !r) {
                std::cerr << r.error().format() << '\n';
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", layer.visible ? "Hide" : "Show");
        }
        ImGui::SameLine();
        const char* lock_glyph = layer.locked ? "[L]" : "[ ]";
        if (ImGui::SmallButton(lock_glyph)) {
            if (auto r = doc.set_layer_locked(layer.id, !layer.locked); !r) {
                std::cerr << r.error().format() << '\n';
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", layer.locked ? "Unlock layer" : "Lock layer (refuse paint)");
        }
        ImGui::SameLine();

        if (renaming.target == layer.id) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool committed =
                ImGui::InputText("##rename",
                                 renaming.buf.data(),
                                 renaming.buf.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue |
                                     ImGuiInputTextFlags_AutoSelectAll);
            const bool blurred = ImGui::IsItemDeactivated();
            if (committed || blurred) {
                std::string trimmed{renaming.buf.data()};
                if (!trimmed.empty()) {
                    if (auto r = doc.set_layer_name(layer.id, std::move(trimmed)); !r) {
                        std::cerr << r.error().format() << '\n';
                    }
                }
                renaming.target = noted::invalid_layer_id;
                renaming.buf[0] = '\0';
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                renaming.target = noted::invalid_layer_id;
                renaming.buf[0] = '\0';
            }
        } else {
            const auto label = layer.name.empty()
                                   ? ("(layer " + std::to_string(layer.id) + ")")
                                   : layer.name;
            if (ImGui::Selectable(label.c_str(),
                                  is_active,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    renaming.target = layer.id;
                    std::strncpy(renaming.buf.data(),
                                 layer.name.c_str(),
                                 renaming.buf.size() - 1U);
                    renaming.buf[renaming.buf.size() - 1U] = '\0';
                } else if (!is_active) {
                    if (auto r = doc.set_active_layer(layer.id); !r) {
                        std::cerr << r.error().format() << '\n';
                    }
                }
            }
        }

        // Row 2: blend dropdown + opacity slider.
        ImGui::SetNextItemWidth(110.0F);
        if (ImGui::BeginCombo("##blend", blend_label(layer.blend))) {
            for (const auto mode : kAllBlendModes) {
                const bool selected = (layer.blend == mode);
                if (ImGui::Selectable(blend_label(mode), selected)) {
                    if (auto r = doc.set_layer_blend(layer.id, mode); !r) {
                        std::cerr << r.error().format() << '\n';
                    }
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Blend mode — persisted now; full GPU blending\nlands in Phase C "
                "(shaders/layer.slang).\nOnly 'Normal' renders correctly today.");
        }
        ImGui::SameLine();
        float opacity = layer.opacity;
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderFloat("##op", &opacity, 0.0F, 1.0F, "%.2f")) {
            if (auto r = doc.set_layer_opacity(layer.id, opacity); !r) {
                std::cerr << r.error().format() << '\n';
            }
        }

        ImGui::EndChild();
        if (is_active) {
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }

    ImGui::End();
    return action;
}

}  // namespace noted::ui::widget
