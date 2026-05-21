#pragma once

// Smart-shape recognizer — App-layer glue that watches stroke
// completion + pointer idle and fires shape recognition when the user
// "draws and pauses" (the Goodnotes / Procreate "draw + hold" gesture).
//
// **Layering**: this is app-layer logic — it knows about
// `Document` indices and routes through the session's UndoStack —
// but its core decision is **pure-logic + time-driven**, with
// `tick(doc, now_seconds)` as the only externally-observable
// transition function. Tests inject monotonic time directly; no
// GLFW / no ImGui / no Vulkan. The App is responsible for calling
// `on_stroke_added` / `on_pointer_active` / `tick` from the
// appropriate hook + frame plumbing.
//
// **Why an explicit state machine rather than a timer**:
//   - GLFW's event-pump thread + ImGui's per-frame tick aren't a
//     reliable real-time clock, so we drive everything off the
//     frame-loop's `glfwGetTime()` instead of OS timers. Pure-logic
//     trigger conditions ("more than `hold_seconds` since last
//     event AND a pending stroke exists") make the decision
//     trivially testable.
//   - The user can interrupt at any point by starting another
//     stroke or moving the pointer — both reset the pending state.
//
// **Index stability**: the recognizer tracks the Document index of
// the most-recent stroke. By the time `tick` actually fires, the
// user may have undone the stroke (size shrinks below the saved
// index) — in that case we silently abandon the pending recognition.
// We do NOT match by sample contents because that's quadratic in
// stroke size and unnecessary; the simpler "last index, drop on
// shrink" rule produces the right UX.

#include <cstddef>
#include <optional>

#include "noted/domain/shape_detect/shape_detect.hpp"
#include "noted/domain/tool/shape_drag.hpp"

namespace noted::domain {
class Document;
}  // namespace noted::domain

namespace noted::app::input {

struct ShapeRecognizerConfig {
    // When false, the recognizer ignores every call to `tick` —
    // existing strokes never get auto-converted. Toggled from
    // brush_options UI.
    bool enabled{false};

    // Seconds the pen must be still after a stroke release before
    // detection kicks in. 0.6 s matches Procreate's default; lower
    // feels eager / surprising, higher feels sluggish.
    double hold_seconds{0.6};

    // Detection tolerance + thresholds.
    noted::domain::shape_detect::DetectionConfig detect{};
};

// What `tick` returns when a recognition succeeds. The caller is
// responsible for executing both mutations through the session's
// UndoStack:
//   1. `RemoveStrokeCommand(stroke_index)` — wipe the user's
//      freehand input.
//   2. `AddShapeCommand(shape)` — drop the recognised primitive in
//      its place.
// The two mutations land as separate undo entries so the user can
// Ctrl+Z once to restore the raw stroke (in case they preferred
// the freehand). A composite-command refinement is a future slice.
struct ShapeRecognitionAction {
    std::size_t stroke_index;
    noted::domain::tool::ShapePrimitive shape;
};

class ShapeRecognizer {
public:
    ShapeRecognizer() = default;

    // Notify that a stroke just landed in `Document::strokes()` at
    // the given index. Resets the per-stroke recognition state so
    // a subsequent `tick` will (after the hold elapses) attempt
    // detection on this stroke specifically.
    void on_stroke_added(std::size_t stroke_index, double now_seconds) noexcept;

    // Notify that the pointer is active (press / move). Cancels the
    // pending recognition — if the user is still drawing they aren't
    // signalling "I'm done, convert this".
    void on_pointer_active(double now_seconds) noexcept;

    // Per-frame check. When the configured hold has elapsed since
    // the last event AND a stroke is pending AND it still exists
    // in the document, runs `detect_shape` and returns the action
    // to execute. Otherwise returns `nullopt`. The recognizer
    // marks the pending stroke as **consumed** after one detection
    // attempt regardless of outcome, so an unmatched stroke isn't
    // re-tested every frame.
    [[nodiscard]] auto tick(const noted::domain::Document& doc,
                            double now_seconds) -> std::optional<ShapeRecognitionAction>;

    void set_config(const ShapeRecognizerConfig& c) noexcept { cfg_ = c; }
    [[nodiscard]] auto config() const noexcept -> const ShapeRecognizerConfig& { return cfg_; }

private:
    ShapeRecognizerConfig cfg_{};
    // Pending stroke index — the most recently emitted stroke that
    // hasn't yet been detected-or-rejected. Cleared on detection
    // attempt or pointer activity.
    std::optional<std::size_t> pending_stroke_index_{};
    // Timestamp of the latest event (stroke add OR pointer activity).
    // The hold elapses relative to this.
    double last_event_seconds_{0.0};
};

}  // namespace noted::app::input
