#pragma once

// Status bar — pinned to the bottom of the main viewport.
//
// Shows:
//   - frame index (engine.frame_index())
//   - FPS (ImGui's built-in Framerate average)
//   - any registered harness::Counter readouts (read-only)
//
// Rendered as an immovable ImGui window without title bar/resize/
// move flags — it sits at the bottom-left of the viewport like a
// real status strip. v0.x; replace with proper docking layout once
// docking integration matures.

#include <cstdint>

namespace noted::ui::widget {

struct StatusBarInfo {
    std::uint64_t frame_index{0};
    // FPS comes from ImGuiIO::Framerate at render time. No need to
    // pre-compute it on the host side; the widget reads it itself.
};

// Render the status bar. Always visible; toggling lives behind the
// View menu only if the user explicitly wants to hide it (out of
// scope for v0.x — every product app has a visible status bar).
void status_bar(const StatusBarInfo& info);

}  // namespace noted::ui::widget
