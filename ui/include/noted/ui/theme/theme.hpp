#pragma once

// noted theme — opinionated ImGuiStyle preset.
//
// Two named themes today: `dark` (default) and `light`. The palette is
// neutral grays with a single soft-blue accent so the engine's
// content (canvas, layer composite, ink) stays the visual focus and
// the chrome reads as a tool rather than a debug overlay (HANDOFF's
// "looks like debug tool" risk).
//
// `apply()` mutates ImGui's global `ImGuiStyle` — sizing fields plus
// the full `Colors[]` table. Cheap; safe to call once per theme
// change rather than per-frame. Style overrides done inside widget
// draw via `PushStyleColor` / `PushStyleVar` remain unaffected
// because Push/Pop is scoped to the draw call.
//
// No external state is owned here — the caller picks the active
// `ThemeKind` (usually persisted in `MenuBarState::theme`) and
// re-applies on change.

namespace noted::ui::theme {

enum class ThemeKind : int {
    dark = 0,
    light = 1,
};

// Mutate ImGui's global style. Sets every `Colors[ImGuiCol_*]` slot
// and the layout sizing fields so the result is deterministic
// regardless of what theme (if any) was applied before.
void apply(ThemeKind kind);

// Human-readable label suitable for menu items. Static storage —
// safe to keep the returned pointer for the menu's lifetime.
[[nodiscard]] auto label(ThemeKind kind) noexcept -> const char*;

}  // namespace noted::ui::theme
