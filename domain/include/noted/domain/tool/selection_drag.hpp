#pragma once

// Selection-tool drag logic — the pure-data side of "user dragged from
// (x1, y1) to (x2, y2) and let go".
//
// Phase B.4 of the unified-canvas plan. The select tool's pointer
// event handling lives in the App (it needs Camera + Hook
// integration), but the **decisions** the App makes — what rectangle
// did the drag produce, how does the modifier set affect the
// outcome — are pure functions of doubles + a Selection. Extracting
// them here keeps the App handler short AND lets unit tests pin the
// behaviour without spinning up ImGui or a Vulkan device.
//
// All coordinates are in canvas pixels (post-Camera unprojection).
// The App handles the screen → canvas conversion before calling
// these helpers.

#include <cstdint>

#include "noted/domain/selection/selection.hpp"

namespace noted::domain::tool {

// How the drag's resulting rectangle combines with the existing
// `Selection`. Maps directly to `Selection::add_rect` / `intersect_rect`
// / `subtract_rect` (plus `clear` for `replace`).
//
// Snapshotted at PRESS time from the modifier-key state ImGui owns:
//   - no modifier   → replace
//   - Shift held    → add (union)
//   - Alt   held    → subtract
//   - Shift + Alt   → intersect
//
// Snapshotting at press (not release) matches Photoshop / Figma — a
// user who releases a modifier mid-drag keeps the action they meant
// when they started.
enum class SelectionDragMode : std::uint8_t {
    replace = 0,
    add = 1,
    subtract = 2,
    intersect = 3,
};

// Build a canonical SelectionRect from a drag span. (x1, y1) is the
// press point, (x2, y2) is the release point — order doesn't matter,
// the helper sorts them into a non-negative-extent rect.
//
// Sub-pixel drags (Manhattan distance < 1 px on either axis) collapse
// to an empty rect — a click that didn't really drag shouldn't
// create a 0×N or N×0 sliver that's "selected" yet invisible.
[[nodiscard]] auto rect_from_drag(double x1,
                                  double y1,
                                  double x2,
                                  double y2) noexcept -> SelectionRect;

// Apply the drag's resulting rectangle to a mutable Selection
// according to `mode`. Empty `r` is a no-op (so a sub-pixel click
// doesn't wipe an existing selection in replace mode). `clear() +
// add_rect()` is the literal implementation of replace — kept inline
// so the contract is explicit.
void apply_drag(Selection& sel, SelectionRect r, SelectionDragMode mode);

// Resolve the modifier-key state into a SelectionDragMode. Pulled
// out so the App side (which knows ImGui's IO) stays declarative
// and the mapping is a single tested contract.
[[nodiscard]] auto drag_mode_from_modifiers(bool shift, bool alt) noexcept -> SelectionDragMode;

}  // namespace noted::domain::tool
