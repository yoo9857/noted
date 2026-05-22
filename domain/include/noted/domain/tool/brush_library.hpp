#pragma once

// BrushLibrary — ordered collection of `BrushPreset`s.
//
// Owns the canonical list the user picks from in the brush-options
// panel. Built-in defaults (Ink Pen / Soft Pencil / Marker / Soft
// Brush / Hard Brush / Airbrush / Calligraphy) are seeded by
// `BrushLibrary::with_builtins()`; user-added presets get the next
// monotonic id and persist alongside built-ins.
//
// Lifecycle:
//   1. App startup → `BrushLibrary::with_builtins()` provides the
//      base set.
//   2. If `<exe_dir>/brushes.json` exists, the loader appends the
//      user's saved presets (with their original ids restored, the
//      id allocator bumped past them).
//   3. UI calls `add` / `remove` / `update`; App writes the library
//      back to JSON when the user creates or deletes a preset.
//
// Mutators return `Result` per the project's no-exceptions contract.
// `find` returns nullptr for unknown ids — callers should always
// handle the absent case (presets can be removed at any time).

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "noted/domain/tool/brush_preset.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::domain::tool {

class BrushLibrary {
public:
    BrushLibrary() = default;
    BrushLibrary(const BrushLibrary&) = default;
    auto operator=(const BrushLibrary&) -> BrushLibrary& = default;
    BrushLibrary(BrushLibrary&&) noexcept = default;
    auto operator=(BrushLibrary&&) noexcept -> BrushLibrary& = default;
    ~BrushLibrary() = default;

    // Factory: returns a library pre-populated with the project's
    // built-in set. Same set every call; ids are stable across
    // process lifetime (they're allocated in the order the
    // built-ins are appended).
    [[nodiscard]] static auto with_builtins() -> BrushLibrary;

    [[nodiscard]] auto size() const noexcept -> std::size_t { return presets_.size(); }
    [[nodiscard]] auto empty() const noexcept -> bool { return presets_.empty(); }
    [[nodiscard]] auto presets() const noexcept -> std::span<const BrushPreset> { return presets_; }

    // Append a preset and assign it the next id. `preset.id` on
    // entry is IGNORED; the caller gets the assigned id back. Use
    // `insert` (loader path) to restore an id verbatim.
    [[nodiscard]] auto add(BrushPreset preset) -> Result<BrushPresetId>;

    // Loader bypass: insert an externally-allocated preset
    // (typically restored from disk). The id must be non-zero AND
    // unique within the library; otherwise rejected. Bumps
    // `next_id_` past the inserted id so subsequent `add` calls
    // never collide.
    [[nodiscard]] auto insert(BrushPreset preset) -> Result<void>;

    // Remove by id. Returns the removed preset for undo/persistence
    // bookkeeping; errors on unknown id.
    [[nodiscard]] auto remove(BrushPresetId id) -> Result<BrushPreset>;

    // Replace the fields of an existing preset (name / kind /
    // parameters). `id` must already be in the library; otherwise
    // rejected. The preset's `id` field on entry is ignored — the
    // id in the call is authoritative.
    [[nodiscard]] auto update(BrushPresetId id, BrushPreset updated) -> Result<void>;

    [[nodiscard]] auto find(BrushPresetId id) const noexcept -> const BrushPreset*;
    [[nodiscard]] auto find_by_name(std::string_view name) const noexcept -> const BrushPreset*;

    // Built-in presets carry ids that callers can use to surface
    // them in the UI grouping (e.g. "Built-in" vs "Mine"). Returns
    // true if the id was assigned by `with_builtins()`. User-added
    // presets always return false even if they happen to share a
    // name with a built-in.
    [[nodiscard]] auto is_builtin(BrushPresetId id) const noexcept -> bool;

    // Replace the entire list. **Loader bypass / test helper only**;
    // bumps `next_id_` past every id in the supplied vector and
    // updates the built-in marker set to whatever indices are
    // currently marked builtin (defaults to "none" — the caller
    // typically calls `mark_builtins_through` afterwards if they
    // want to preserve which entries are factory).
    void replace(std::vector<BrushPreset> presets) noexcept;

    // Mark the first `count` presets as built-in. Used by
    // `with_builtins()` after seeding so `is_builtin` works
    // straight away, and by the JSON loader after replacing the
    // factory layer with the saved file's contents.
    void mark_builtins_through(std::size_t count) noexcept;

private:
    std::vector<BrushPreset> presets_{};
    BrushPresetId next_id_{1};
    // Index threshold: presets[0..builtin_count_) are factory-shipped;
    // everything from `builtin_count_` onward is user-added. Stored
    // as a count rather than per-preset flags so loaders can mark a
    // contiguous prefix in one call.
    std::size_t builtin_count_{0};
};

}  // namespace noted::domain::tool
