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
#include <string_view>

namespace noted::ui::widget {

struct StatusBarInfo {
    std::uint64_t frame_index{0};
    // FPS comes from ImGuiIO::Framerate at render time. No need to
    // pre-compute it on the host side; the widget reads it itself.

    // Canvas zoom as a percentage (1.0 → "100%"). Host supplies it
    // from the active `noted::canvas::Camera`. 0 (or any
    // non-positive) skips the readout — leaves room for tests /
    // headless contexts that don't bind a camera.
    float zoom_pct{100.0F};

    // Active canvas layer summary. Empty `active_layer_name` (the
    // default) suppresses the readout entirely — keeps tests /
    // headless contexts from rendering "Painting: ?" with no
    // session bound.
    //
    // When non-empty the bar reads "Painting: <name>" and, if the
    // active layer is non-paintable (hidden or locked or pointing
    // at an unresolvable id), appends a high-contrast warning tag
    // so the user notices BEFORE they try to draw and wonder why
    // nothing happens. Worst v0.x usability foot-gun before this
    // landed: a hidden layer silently consumed input.
    std::string_view active_layer_name;
    bool active_layer_visible{true};
    bool active_layer_locked{false};
    // True when there's a stack but no active id resolves — usually
    // a transient state right after a Remove. The bar surfaces this
    // explicitly so the user understands why paint fails.
    bool active_layer_orphaned{false};
};

// Render the status bar. Always visible; toggling lives behind the
// View menu only if the user explicitly wants to hide it (out of
// scope for v0.x — every product app has a visible status bar).
void status_bar(const StatusBarInfo& info);

}  // namespace noted::ui::widget
