#pragma once

// Text overlay — renders committed text + the in-flight editing
// InputText on top of the canvas.
//
// Phase B.6. Unlike `selection_overlay` and `shape_overlay`, this
// widget is **interactive** (it owns the ImGui InputText for typing)
// so it can't live entirely on `ImGui::GetBackgroundDrawList()`.
// Strategy: render committed primitives via the background draw
// list (non-interactive) AND host the InputText inside a borderless
// transparent ImGui window positioned at the editing anchor.
//
// Pure-data API — the overlay only sees `domain::tool` types + two
// callbacks (`on_commit` / `on_cancel`). Keeps `ui` free of any
// `app/` dependency; the `TextToolHandler` plumbing happens one
// layer up.

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace noted::domain::tool {
struct TextPrimitive;
struct TextEditingState;
}  // namespace noted::domain::tool

namespace noted::ui::widget {

// Canvas-pixel (x, y) → screen-pixel (x, y). Same callback shape as
// the other overlays.
using TextCanvasToScreenFn = std::function<std::pair<float, float>(double, double)>;

// Render committed texts + the in-flight InputText. `enabled` lets
// the host hide the committed-text layer; the InputText still
// renders if editing is in progress (a hidden overlay must not eat
// keystrokes the user typed before noticing the toggle).
//
// `editing` is mutable — the overlay writes into its `buffer` via
// ImGui InputText and clears `needs_focus` after consuming it. The
// `on_commit` / `on_cancel` callbacks are fired on Enter / Esc; the
// host (typically a `TextToolHandler`) translates them into
// commit / cancel of the editing session.
void text_overlay(const std::vector<noted::domain::tool::TextPrimitive>& texts,
                  std::optional<noted::domain::tool::TextEditingState>& editing,
                  const TextCanvasToScreenFn& canvas_to_screen,
                  const std::function<void()>& on_commit,
                  const std::function<void()>& on_cancel,
                  bool enabled);

}  // namespace noted::ui::widget
