#pragma once

// 12 o'clock floating toolbar — replaces the left-rail `tool_palette`.
//
// **Phase 3 of ADR 0034.** Mac-style floating pill anchored to the
// top centre of the canvas, just below the chrome strip and the menu
// bar. Hosts one icon-style button per tool plus a separator and
// the undo / redo buttons. Active tool is highlighted with the
// system-accent fill; inactive buttons are outlined; hover state
// matches macOS Sonoma's subtle hover-brighten.
//
// Stateless — same pattern as `mac_chrome` / `menu_bar` / `page_strip`.
// The host owns `ToolState` + the `UndoStack`; the widget reads the
// current active tool and undo/redo predicates, returns a
// `TopToolbarResult` describing the user action, and the host
// performs the side effect (tool switch, undo, redo).
//
// Visual spec:
//   - Pill background: \#1F1F22, 12 px corner radius, 1 px border
//     in \#2F2F33, drop shadow with 8 px blur.
//   - Button size: 36 × 32 px each.
//   - Active button: filled with the system-accent (currently the
//     macOS-blue \#0A84FF).
//   - Gap between adjacent buttons: 4 px.
//   - Group separator: 10 px gap + 1 px vertical line.
//   - Pill horizontal padding: 8 px inside.

#include <optional>

#include "noted/domain/tool/tool.hpp"

namespace noted::ui::widget {

struct TopToolbarStatus {
    bool can_undo{false};
    bool can_redo{false};
};

struct TopToolbarResult {
    // The user clicked a different tool's button. Host assigns to
    // `ToolState::active`.
    std::optional<noted::domain::tool::ToolKind> switch_request{};
    bool undo_clicked{false};
    bool redo_clicked{false};
};

// Renders the toolbar in an ImGui window placed automatically at
// the top centre of the main viewport. `top_offset_px` is how far
// down from the very top of the swapchain to start the pill (the
// host passes `mac_chrome_height_px() + menu_bar_height_px()` so the
// pill sits *just below* the menu bar). The widget owns its own
// Begin/End; caller does no layout work.
[[nodiscard]] auto top_toolbar(noted::domain::tool::ToolKind active,
                               const TopToolbarStatus& status,
                               float top_offset_px) -> TopToolbarResult;

}  // namespace noted::ui::widget
