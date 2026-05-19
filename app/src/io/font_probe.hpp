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

}  // namespace noted::app
