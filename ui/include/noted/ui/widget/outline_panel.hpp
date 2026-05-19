#pragma once

// Document outline panel — tree view of the block hierarchy.
//
// Renders the document's blocks as a collapsible tree mirroring the
// parent / children structure. Each row shows:
//   - block kind label (group / text / heading / code / canvas /
//     image / embed)
//   - block name (or "(unnamed)")
//   - selection state (click selects, persisted in `selected_id`)
//
// Inline rename mode: when a non-null `OutlineRenameState` is
// supplied and its `target` matches a node's id, the widget swaps
// the row's name display for an `ImGui::InputText` bound to the
// caller's `buffer`. The widget surfaces commit / cancel decisions
// as bool flags the caller drains each frame. The state struct is
// caller-owned so it survives panel toggles + frames; widget
// stores nothing internally.

#include <array>
#include <cstddef>

#include "noted/domain/document/document.hpp"

namespace noted::ui::widget {

struct OutlineRenameState {
    // Caller sets this to the block id whose row should render as
    // an InputText. `invalid_block_id` = no rename in progress; the
    // widget renders every row in its normal display form.
    noted::domain::BlockId target{noted::domain::invalid_block_id};

    // The text buffer ImGui::InputText writes into. Caller pre-fills
    // it with the current name when arming a rename. 256 bytes is
    // wildly generous for a block name; sized as `std::array` so
    // the lifetime is the caller's by construction (no `new[]`).
    std::array<char, 256> buffer{};

    // Set by the caller on the first frame after arming. Widget
    // calls `ImGui::SetKeyboardFocusHere()` once, then flips this
    // false so subsequent frames don't pin focus on the input.
    bool needs_focus{false};

    // ---- Outputs (widget sets; caller drains each frame) -----------
    // True when the user pressed Enter or moved focus elsewhere
    // after editing — caller should treat the buffer contents as
    // the final new name.
    bool commit_requested{false};
    // True when the user pressed Escape — caller should drop the
    // buffer + clear `target` without writing the document.
    bool cancel_requested{false};
};

// Render the outline panel inside a Begin/End scope managed by this
// function. `open` controls visibility (pass
// `&state.show_outline_panel`). `selected_id` is mutated when the
// user clicks a row; pass `invalid_block_id` initially and the
// panel will adopt the root on first click. `rename` is nullable —
// nullptr disables rename mode (back-compat with existing callers).
void outline_panel(const noted::domain::Document& doc,
                   noted::domain::BlockId& selected_id,
                   OutlineRenameState* rename,
                   bool* open);

}  // namespace noted::ui::widget
