#pragma once

// Layer panel — observes `Document::canvas_layers()`, surfaces every
// row UI (visibility eye, lock, name, opacity slider, blend mode,
// reorder, duplicate, remove) and reports the user's intent back
// to the host as a `LayerPanelAction`.
//
// Why intents instead of direct Document mutation:
//   - Every mutation must flow through the host's `UndoStack` so
//     Ctrl+Z works AND so the document's dirty proxy (which counts
//     undo entries) correctly flags layer-only edits. The widget
//     does not own the stack — `DocumentSession` does — so the
//     widget reports intent and the host wraps it in a Command.
//   - Coalescing for high-frequency edits (opacity slider drag,
//     rename keystrokes) is the Command layer's job too. The widget
//     emits an action per frame; the UndoStack folds adjacent
//     same-target opacity / rename intents into one history entry
//     (see `Command::try_merge`).
//
// At most ONE action fires per frame — ImGui's input dispatch
// produces a single edit event from the per-row controls under
// normal use. If a user clicks two buttons in the same frame the
// later one wins (the action variant is reassigned on each branch).
//
// The widget never throws and never returns Result. It is a pure
// observer + intent emitter; failure paths (unknown id, OOB index)
// can only arise from a host bug and surface through the host's
// command-execution error channel.

#include <cstddef>
#include <cstdint>
#include <string>

#include "noted/domain/layer/layer.hpp"  // BlendMode
#include "noted/engine/canvas/layer_id.hpp"

namespace noted::domain {
class Document;
}

namespace noted::ui::widget {

// Structural + per-field intents surfaced back to the host. At most
// ONE action fires per frame; `kind == none` means the user did
// nothing this frame.
//
// The struct is a tagged union by `Kind`; only fields named in the
// per-kind doc below are valid for each kind.
struct LayerPanelAction {
    enum class Kind : std::uint8_t {
        none = 0,
        // Structural — index/payload fields documented per-arm.
        add = 1,        // add_name
        remove = 2,     // index
        duplicate = 3,  // index
        move_up = 4,    // index (relative; widget knows the bounds)
        move_down = 5,  // index
        // Per-layer field edits — `target_id` carries the layer; the
        // host wraps each in the matching `SetLayer*Command` so undo
        // / dirty tracking fire automatically.
        set_visible = 6,  // target_id, bool_value
        set_locked = 7,   // target_id, bool_value
        set_name = 8,     // target_id, string_value
        set_opacity = 9,  // target_id, float_value
        set_blend = 10,   // target_id, blend_value
        set_active = 11,  // target_id  (direct, not undoable — see host)
        merge_down = 12,  // index  (the SOURCE; target = index - 1)
    };
    Kind kind{Kind::none};

    // ---- Structural payload ----
    // For `add`: the suggested name (`"Layer N"`).
    // For `remove` / `duplicate` / `move_*`: the index in the stack
    // (0 = bottom, size-1 = top).
    std::string add_name;
    std::size_t index{0};

    // ---- Per-layer-edit payload ----
    // `target_id` is the layer the edit targets. `*_value` carries
    // the new value per the kind doc above.
    noted::LayerId target_id{noted::invalid_layer_id};
    bool bool_value{false};
    float float_value{0.0F};
    noted::domain::BlendMode blend_value{noted::domain::BlendMode::normal};
    std::string string_value;
};

[[nodiscard]] auto layer_panel(const noted::domain::Document& doc, bool* open) -> LayerPanelAction;

}  // namespace noted::ui::widget
