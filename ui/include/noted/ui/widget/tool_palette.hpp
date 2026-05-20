#pragma once

// Tool palette — left side rail of editing-tool buttons.
//
// Renders one button per `noted::domain::tool::ToolKind` in a fixed
// vertical layout. The button matching the active tool is rendered
// highlighted via ImGuiCol_Button. Clicking a button emits a
// `ToolPaletteResult::switch_request` the host applies to its
// `ToolState`.
//
// Stateless — matches the `menu_bar` / `page_strip` pattern. Host
// owns the `ToolState`; the widget only reads `active` to decide
// which button to highlight.

#include <optional>

#include "noted/domain/tool/tool.hpp"

namespace noted::ui::widget {

struct ToolPaletteResult {
    // The user clicked a different tool's button. The host should
    // assign this to `ToolState::active` and react (e.g. update the
    // stroke engine's brush colour).
    std::optional<noted::domain::tool::ToolKind> switch_request{};
};

// Render the palette inside an ImGui::Begin / End scope managed by
// this function. `open` controls visibility — pass
// `&state.show_tool_palette` from menu_bar's MenuBarState so View →
// Tool palette toggles it.
[[nodiscard]] auto tool_palette(noted::domain::tool::ToolKind active,
                                bool* open) -> ToolPaletteResult;

}  // namespace noted::ui::widget
