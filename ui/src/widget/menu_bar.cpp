#include "noted/ui/widget/menu_bar.hpp"

#include <imgui.h>

namespace noted::ui::widget {

auto menu_bar(MenuBarState& state) -> MenuBarResult {
    MenuBarResult result{};
    if (!ImGui::BeginMainMenuBar()) {
        return result;
    }

    if (ImGui::BeginMenu("File")) {
        ImGui::MenuItem("New", "Ctrl+N", false, /*enabled=*/false);  // next PR
        ImGui::MenuItem("Open", "Ctrl+O", false, /*enabled=*/false);
        ImGui::MenuItem("Save", "Ctrl+S", false, /*enabled=*/false);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
            result.quit_requested = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        ImGui::MenuItem("Undo", "Ctrl+Z", false, /*enabled=*/false);  // next PR
        ImGui::MenuItem("Redo", "Ctrl+Y", false, /*enabled=*/false);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Layers", nullptr, &state.show_layer_panel);
        ImGui::MenuItem("ImGui Demo", nullptr, &state.show_demo_window);
        ImGui::Separator();
        ImGui::MenuItem("About noted", nullptr, &state.show_about_window);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
    return result;
}

}  // namespace noted::ui::widget
