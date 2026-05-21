#pragma once

// Cross-platform probe for an OS-installed CJK TrueType / OpenType font.
// Used at startup to feed `ui::ImGuiHostCreateInfo::cjk_font_path`.
//
// Returns the first existing candidate, or an empty path when none
// are present — ImGuiHost falls back to its default ProggyClean
// bitmap font in that case.

#include <filesystem>

namespace noted::app {

[[nodiscard]] auto probe_cjk_font() -> std::filesystem::path;

// Symbol-rich fallback font for UI icon glyphs that the CJK font
// doesn't carry. Merged into the ImGui atlas after the primary CJK
// face — when the primary lacks a glyph, ImGui falls through to
// this face. Empty path means no fallback (icons render as tofu).
[[nodiscard]] auto probe_symbol_font() -> std::filesystem::path;

}  // namespace noted::app
