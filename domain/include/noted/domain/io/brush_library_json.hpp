#pragma once

// JSON serialization for `noted::domain::tool::BrushLibrary`.
//
// User-defined presets persist to `<exe_dir>/brushes.json` so they
// survive restarts AND can be hand-edited / shared / version-
// controlled. Built-in presets are NOT round-tripped through this
// file — they're seeded by `BrushLibrary::with_builtins()` every
// launch, then the saved-user-presets file appends any user additions
// on top. This keeps factory updates flowing to old installs
// without requiring users to delete their save.
//
// Schema v1 (`{ "version": 1, "presets": [ ... ] }`) is parsed
// strictly: unknown top-level keys + unknown per-preset keys are
// rejected with `invalid_argument` so a typo or partial future-
// version field can't silently round-trip.

#include <string>
#include <string_view>

#include "noted/engine/error/error.hpp"

namespace noted::domain::tool {
class BrushLibrary;
}  // namespace noted::domain::tool

namespace noted::domain::io {

// On-disk schema version for the brush-library file. Writer always
// emits this; reader accepts this value AND every prior supported
// version once we ship one.
inline constexpr int kBrushLibraryJsonVersion = 1;

// Serialize ONLY the user-added presets (everything past the
// `builtin_count_` watermark). The factory section is regenerated
// on every launch — including it in the save file would let stale
// factory metadata override a future update.
[[nodiscard]] auto brush_library_user_to_json(const noted::domain::tool::BrushLibrary& lib)
    -> std::string;

// Parse user-added presets and append them to `lib`. Their original
// ids are restored (so live references survive); the id allocator
// is bumped past every restored id. Caller is expected to have
// already seeded the factory presets via `with_builtins()` — this
// function does NOT touch the factory section.
//
// Errors with `invalid_argument` on malformed JSON, unknown keys,
// duplicate ids, or zero-id entries; the library is left untouched
// on any failure (validate-then-mutate).
[[nodiscard]] auto brush_library_user_from_json(
    std::string_view json_text, noted::domain::tool::BrushLibrary& lib) -> Result<void>;

}  // namespace noted::domain::io
