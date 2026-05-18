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
// The selected block id is the caller's persistent state — it
// follows the user across frames and survives panel toggles. v0.x
// doesn't yet drive any editor based on this selection; future
// widgets (block properties, "Add Child Block" submenu target,
// etc.) consume it.

#include "noted/domain/document/document.hpp"

namespace noted::ui::widget {

// Render the outline panel inside a Begin/End scope managed by this
// function. `open` controls visibility (pass `&state.show_outline_panel`).
// `selected_id` is mutated when the user clicks a row; pass
// `invalid_block_id` initially and the panel will adopt the root on
// first click.
void outline_panel(const noted::domain::Document& doc,
                   noted::domain::BlockId& selected_id,
                   bool* open);

}  // namespace noted::ui::widget
