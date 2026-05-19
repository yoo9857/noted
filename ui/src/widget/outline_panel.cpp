#include "noted/ui/widget/outline_panel.hpp"

#include <cfloat>

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

// Render the row in normal (non-rename) form. Returns the opened
// state for tree-walk recursion.
[[nodiscard]] auto draw_normal_row(const noted::domain::BlockNode& node,
                                   noted::domain::BlockId id,
                                   noted::domain::BlockId& selected_id) -> bool {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (id == selected_id) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const char* name = node.name.empty() ? "(unnamed)" : node.name.c_str();
    const bool opened = ImGui::TreeNodeEx("##node", flags, "%s — %s", kind_label(node.kind), name);

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        selected_id = id;
    }
    return opened;
}

// Render the row in rename form: kind label as a static prefix, the
// rest of the row taken by an InputText bound to `rename->buffer`.
// The widget never reads or writes the underlying document — that
// is the host's job once it sees `commit_requested`.
[[nodiscard]] auto draw_rename_row(const noted::domain::BlockNode& node,
                                   OutlineRenameState& rename) -> bool {
    // TreeNode with an empty label keeps the indent + open-arrow but
    // leaves the rest of the row for SameLine widgets. Selection
    // state is suppressed while renaming — the focused InputText is
    // the visual highlight.
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (node.children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    const bool opened = ImGui::TreeNodeEx("##node", flags, "%s — ", kind_label(node.kind));

    ImGui::SameLine();
    // -FLT_MIN expands the InputText to fill the remaining row width
    // — matches the standard ImGui idiom for "use all available
    // horizontal space."
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (rename.needs_focus) {
        ImGui::SetKeyboardFocusHere();
        rename.needs_focus = false;
    }
    constexpr ImGuiInputTextFlags kInputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                                ImGuiInputTextFlags_AutoSelectAll |
                                                ImGuiInputTextFlags_NoUndoRedo;
    // NoUndoRedo intentionally — ImGui's per-input undo collides
    // with the document undo stack the host runs. The rename's
    // accept / cancel surface is already binary (Enter / Escape).
    const bool enter_pressed =
        ImGui::InputText("##rename", rename.buffer.data(), rename.buffer.size(), kInputFlags);
    if (enter_pressed) {
        rename.commit_requested = true;
    }
    // Focus loss = implicit commit. IsItemDeactivatedAfterEdit fires
    // when the user clicked away mid-edit; treat the same as Enter
    // so partial work isn't silently dropped.
    if (ImGui::IsItemDeactivatedAfterEdit() && !rename.commit_requested) {
        rename.commit_requested = true;
    }
    // Escape cancels. Check globally — InputText doesn't intercept
    // Escape unless we asked it to (we didn't, on purpose).
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        rename.cancel_requested = true;
    }
    return opened;
}

void draw_node(const noted::domain::Document& doc,
               noted::domain::BlockId id,
               noted::domain::BlockId& selected_id,
               OutlineRenameState* rename) {
    const auto* node = doc.find(id);
    if (node == nullptr) {
        return;
    }
    ImGui::PushID(static_cast<int>(id));

    const bool is_rename_target = (rename != nullptr) && (rename->target == id);
    const bool opened = is_rename_target ? draw_rename_row(*node, *rename)
                                         : draw_normal_row(*node, id, selected_id);

    if (opened && !node->children.empty()) {
        for (const auto child : node->children) {
            draw_node(doc, child, selected_id, rename);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

}  // namespace

void outline_panel(const noted::domain::Document& doc,
                   noted::domain::BlockId& selected_id,
                   OutlineRenameState* rename,
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
    draw_node(doc, doc.root(), selected_id, rename);

    ImGui::End();
}

}  // namespace noted::ui::widget
