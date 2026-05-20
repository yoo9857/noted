#pragma once

// Main menu bar — the top-most UI surface.
//
// Renders ImGui::BeginMainMenuBar with File/Edit/View/Help menus.
// Returns a `MenuBarResult` struct of "user wanted X" signals the
// host frame loop acts on. The widget itself is stateless;
// persistent toggles live in `MenuBarState` which the caller owns.
//
// ImGui-idiomatic: this is the immediate-mode "draw and return events"
// pattern. Future widgets that need a richer interaction model can
// graduate to their own .hpp/.cpp pair without touching this one.

#include <optional>

#include "noted/domain/document/document.hpp"
#include "noted/ui/theme/theme.hpp"

namespace noted::ui::widget {

// Persistent toggles the menu controls. Owned by the caller (e.g.
// main.cpp) and passed in each frame so the menu can both display
// the current state and mutate it on click.
struct MenuBarState {
    bool show_layer_panel{true};
    bool show_outline_panel{true};
    bool show_page_strip{true};
    bool show_debug_overlay{false};
    bool show_demo_window{false};
    bool show_about_window{false};
    // Active theme. Mutated by the menu's View → Theme radio items;
    // the host watches for changes and re-applies via
    // `noted::ui::theme::apply()`.
    noted::ui::theme::ThemeKind theme{noted::ui::theme::ThemeKind::dark};
};

// Per-frame inputs for the menu bar. Wires the live state of the
// surrounding model (UndoStack, current document path) into menu
// items' enabled / label state.
struct MenuBarStatus {
    bool can_undo{false};
    bool can_redo{false};
    // True when the current document has an on-disk backing path.
    // Controls whether Save writes in-place (true) or falls through
    // to Save As (false). The label always reads "Save" — the
    // fall-through is invisible to the user, matching the platform
    // convention (Word, Photoshop, VS Code).
    bool has_document_path{false};
};

// One-frame outputs from menu_bar(). The frame loop checks these
// after the call and acts.
struct MenuBarResult {
    bool quit_requested{false};
    bool undo_requested{false};
    bool redo_requested{false};
    // File menu — at most one of these can be set per frame because
    // ImGui menus close on selection.
    bool file_new_requested{false};
    bool file_open_requested{false};
    bool file_save_requested{false};
    bool file_save_as_requested{false};
    // When set, the user picked Edit → Add Block → <kind>. The host
    // creates an `AddBlockCommand` with this kind and pushes it onto
    // the UndoStack.
    std::optional<noted::domain::BlockKind> add_block_requested{};
};

// Render the main menu bar. Mutates `state` in place when the user
// clicks a toggle; sets fields on the return value for one-shot
// events. Safe to call before / after / outside an ImGui frame;
// the internal `BeginMainMenuBar` guard early-outs when ImGui is
// not in a frame.
[[nodiscard]] auto menu_bar(MenuBarState& state, const MenuBarStatus& status) -> MenuBarResult;

}  // namespace noted::ui::widget
