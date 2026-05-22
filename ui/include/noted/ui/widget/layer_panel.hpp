#pragma once

// Layer panel — observes `Document::canvas_layers()`, surfaces row UI
// (visibility eye, lock, name, opacity slider, blend mode, reorder,
// duplicate, remove) and emits structural mutations as
// `LayerPanelAction`s back to the host.
//
// Why the action struct rather than direct Document mutation:
//   - Add / Duplicate / Remove / Reorder must go through the
//     UndoStack so Ctrl+Z restores the prior structural state.
//     The panel doesn't own the stack — `DocumentSession` does —
//     so it reports the user's intent and the host wraps it in a
//     Command.
//   - Visibility / lock / rename / opacity / blend stay direct
//     (clamp-style edits in the inner loop) because making every
//     checkbox toggle a full UndoStack entry would spam the history.
//     Photoshop draws the same line — only layer creation / deletion
//     / reorder is undoable by default.
//
// The widget never throws and never returns Result — direct mutation
// failures (unknown id, OOB index) log via the harness and the panel
// re-renders best-effort on the next frame.

#include <cstddef>
#include <cstdint>
#include <string>

#include "noted/engine/canvas/layer_id.hpp"

namespace noted::domain {
class Document;
}

namespace noted::ui::widget {

// Structural intents surfaced back to the host. At most ONE action
// fires per frame; `kind == none` means the user did nothing
// structural this frame.
struct LayerPanelAction {
    enum class Kind : std::uint8_t {
        none = 0,
        add = 1,
        remove = 2,
        duplicate = 3,
        move_up = 4,
        move_down = 5,
    };
    Kind kind{Kind::none};
    // For `add`: the suggested name (`"Layer N"`).
    // For `remove` / `duplicate` / `move_*`: the index in the stack
    // (0 = bottom, size-1 = top).
    std::string add_name;
    std::size_t index{0};
};

[[nodiscard]] auto layer_panel(noted::domain::Document& doc, bool* open) -> LayerPanelAction;

}  // namespace noted::ui::widget
