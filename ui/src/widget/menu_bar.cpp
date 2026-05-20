#include "noted/ui/widget/menu_bar.hpp"

#include <array>
#include <utility>

#include <imgui.h>

namespace noted::ui::widget {

namespace {

constexpr std::array<std::pair<const char*, noted::domain::BlockKind>, 7> kAddBlockMenu{{
    {"Group", noted::domain::BlockKind::group},
    {"Text", noted::domain::BlockKind::text},
    {"Heading", noted::domain::BlockKind::heading},
    {"Code", noted::domain::BlockKind::code},
    {"Canvas", noted::domain::BlockKind::canvas},
    {"Image", noted::domain::BlockKind::image},
    {"Embed", noted::domain::BlockKind::embed},
}};

}  // namespace

auto menu_bar(MenuBarState& state, const MenuBarStatus& status) -> MenuBarResult {
    MenuBarResult result{};
    if (!ImGui::BeginMainMenuBar()) {
        return result;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New", "Ctrl+N")) {
            result.file_new_requested = true;
        }
        if (ImGui::MenuItem("Open...", "Ctrl+O")) {
            result.file_open_requested = true;
        }
        // Save is always enabled; when the doc has no backing path
        // the host transparently falls through to Save As. This
        // matches the platform convention (Word, Photoshop, VS Code).
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            if (status.has_document_path) {
                result.file_save_requested = true;
            } else {
                result.file_save_as_requested = true;
            }
        }
        if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) {
            result.file_save_as_requested = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            result.quit_requested = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, status.can_undo)) {
            result.undo_requested = true;
        }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, status.can_redo)) {
            result.redo_requested = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Add Block")) {
            for (const auto& [label, kind] : kAddBlockMenu) {
                if (ImGui::MenuItem(label)) {
                    result.add_block_requested = kind;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Layers", nullptr, &state.show_layer_panel);
        ImGui::MenuItem("Outline", nullptr, &state.show_outline_panel);
        ImGui::MenuItem("Page strip", nullptr, &state.show_page_strip);
        ImGui::MenuItem("Tool palette", nullptr, &state.show_tool_palette);
        ImGui::MenuItem("Brush options", nullptr, &state.show_brush_options);
        ImGui::MenuItem("Debug overlay", nullptr, &state.show_debug_overlay);
        ImGui::MenuItem("ImGui Demo", nullptr, &state.show_demo_window);
        if (ImGui::BeginMenu("Theme")) {
            using noted::ui::theme::ThemeKind;
            constexpr std::array<ThemeKind, 2> kThemes{ThemeKind::dark, ThemeKind::light};
            for (const auto t : kThemes) {
                const bool selected = (state.theme == t);
                if (ImGui::MenuItem(noted::ui::theme::label(t), nullptr, selected)) {
                    state.theme = t;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("About noted", nullptr, &state.show_about_window);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    return result;
}

}  // namespace noted::ui::widget
