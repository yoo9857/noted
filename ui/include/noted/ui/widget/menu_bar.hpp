#pragma once

// Main menu bar — the top-most UI surface.
//
// Renders ImGui::BeginMainMenuBar with File/View/Help menus. Returns
// a `MenuBarResult` struct of "user wanted X" signals the host frame
// loop acts on. The widget itself is stateless; persistent toggles
// live in `MenuBarState` which the caller owns.
//
// ImGui-idiomatic: this is the immediate-mode "draw and return events"
// pattern. Future widgets that need a richer interaction model can
// graduate to their own .hpp/.cpp pair without touching this one.

namespace noted::ui::widget {

// Persistent toggles the menu controls. Owned by the caller (e.g.
// main.cpp) and passed in each frame so the menu can both display
// the current state and mutate it on click.
struct MenuBarState {
    bool show_layer_panel{true};
    bool show_demo_window{false};
    bool show_about_window{false};
};

// One-frame outputs from menu_bar(). The frame loop checks these
// after the call and acts (e.g. break out of the loop on quit).
struct MenuBarResult {
    bool quit_requested{false};
};

// Render the main menu bar. Mutates `state` in place when the user
// clicks a toggle; sets fields on the return value for one-shot
// events. Safe to call before / after / outside an ImGui frame; the
// internal `BeginMainMenuBar` guard early-outs when ImGui is not
// in a frame.
[[nodiscard]] auto menu_bar(MenuBarState& state) -> MenuBarResult;

}  // namespace noted::ui::widget
