#pragma once

// Layer panel — observes + mutates a `domain::LayerGraph`.
//
// Displays the graph's layers in topological order (matches the
// compositor's render order). Each row shows:
//   - visibility checkbox     ← toggling calls LayerGraph::set_visible
//   - layer name
//   - blend mode label (read-only for now; editable in a later PR)
//   - opacity readout         (read-only for now)
//
// The widget never throws and never returns Result — mutations that
// fail (cycle creation in a future "Add Layer" path, etc.) write to
// stderr via the harness; the panel renders best-effort.
//
// Visibility is the only product action the v0.x panel supports;
// adding / removing / re-ordering layers is the next PR after this
// (feat/ui-document-shell follow-up — layer editing via Command).

namespace noted::domain {
class LayerGraph;
}

namespace noted::ui::widget {

// Render the panel inside an ImGui::Begin / End scope managed by
// this function. `open` controls visibility — pass &state.show_layer_panel
// from menu_bar's MenuBarState so View → Layers toggles it.
//
// `graph` is mutable: checkbox toggles call `set_visible()` directly
// (no Command pattern yet — UndoStack wiring is the next PR).
void layer_panel(noted::domain::LayerGraph& graph, bool* open);

}  // namespace noted::ui::widget
