#include "noted/ui/widget/outline_panel.hpp"

#include <imgui.h>

namespace noted::ui::widget {

namespace {

[[nodiscard]] auto kind_label(noted::domain::BlockKind kind) noexcept -> const char* {
    using K = noted::domain::BlockKind;
    switch (kind) {
        case K::group:
            return "Group";
        case K::text:
            return "Text";
        case K::heading:
            return "Heading";
        case K::code:
            return "Code";
        case K::canvas:
            return "Canvas";
        case K::image:
            return "Image";
        case K::embed:
            return "Embed";
    }
    return "?";
}

void draw_node(const noted::domain::Document& doc,
               noted::domain::BlockId id,
               noted::domain::BlockId& selected_id) {
    const auto* node = doc.find(id);
    if (node == nullptr) {
        return;
    }
    ImGui::PushID(static_cast<int>(id));

    // Leaves render as a row; nodes with children render as a
    // collapsible TreeNode. Auto-open one level so the doc structure
    // is visible without manual expand on first paint.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (node->children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (id == selected_id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const char* name = node->name.empty() ? "(unnamed)" : node->name.c_str();
    const bool opened = ImGui::TreeNodeEx("##node", flags, "%s — %s", kind_label(node->kind), name);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected_id = id;
    }

    if (opened && !node->children.empty()) {
        for (const auto child : node->children) {
            draw_node(doc, child, selected_id);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

}  // namespace

void outline_panel(const noted::domain::Document& doc,
                   noted::domain::BlockId& selected_id,
                   bool* open) {
    if (open != nullptr && !*open) {
        return;
    }
    if (!ImGui::Begin("Outline", open)) {
        ImGui::End();
        return;
    }

    if (doc.empty()) {
        ImGui::TextDisabled("(empty document)");
        ImGui::TextWrapped(
            "Use Edit → Add Block to create blocks. The new "
            "block lands under the selected node (or as root "
            "if the document is empty).");
        ImGui::End();
        return;
    }
    draw_node(doc, doc.root(), selected_id);

    ImGui::End();
}

}  // namespace noted::ui::widget
