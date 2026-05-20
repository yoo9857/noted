#pragma once

// Brush options — the active tool's runtime-tweakable controls.
//
// Renders ImGui widgets bound to the per-tool option payload on the
// caller-owned `ToolState`. The widget is **stateless** but mutates
// the state in place (the ImGui idiom for sliders / colour pickers
// — `ImGui::SliderFloat(&value)` writes through the pointer).
//
// Which controls show depends on `ToolState::active`:
//   - Pen    → size range (min / max radius), alpha gamma, colour
//   - Eraser → size range, alpha gamma
//   - others → "(no options yet)" placeholder — those tools' option
//             payloads land alongside their behavioural integration
//             in Phase B.4+.
//
// Unlike `tool_palette` which only emits switch_requests, this
// widget writes directly to ToolState because brush-options edits
// don't need undo/redo (they're live ergonomic settings, not
// document content). If a future requirement promotes them to undo
// entries, this widget gains a `BrushOptionsResult` return and the
// App routes through `session_.execute(...)`.

#include "noted/domain/tool/tool.hpp"

namespace noted::ui::widget {

// Render the brush options inside an ImGui::Begin / End scope managed
// by this function. `open` controls visibility — pass
// `&state.show_brush_options` from MenuBarState. `tools` is mutated
// in place when the user drags a slider / picks a colour.
void brush_options(noted::domain::tool::ToolState& tools, bool* open);

}  // namespace noted::ui::widget
