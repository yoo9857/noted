#pragma once

// Navigator panel — small canvas thumbnail with a viewport-rect
// overlay, for quick navigation of large documents (Goodnotes /
// OpenCanvas / Photoshop "Navigator" pattern).
//
// MVP rendering (no GPU texture sampling — that's a follow-up):
//   - A dark thumbnail box mapping the entire canvas extent into
//     ~220 × 140 px screen space, preserving aspect ratio.
//   - Each page rendered as a small light rectangle at its
//     proportional position.
//   - The current viewport (the screen-visible canvas region)
//     rendered as a bright outlined rectangle on top.
//   - Mouse click / drag inside the thumbnail re-centres the
//     viewport at the corresponding canvas point — the caller
//     applies the resulting camera translation.
//
// Returning a `NavigatorResult` rather than mutating the camera
// directly keeps the widget pure-presentation, same pattern as
// `top_toolbar` and `color_picker_panel`.
//
// A future PR can replace the box+outline thumbnail with a real
// canvas-texture sampled through `ImGui::Image` once we wire an
// `ImTextureID` for the canvas + handle resize. The widget's
// public shape (input args + return type) stays unchanged when
// that swap lands.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace noted::canvas {
// Page is a struct (see engine/canvas/page.hpp). Forward-declaring
// as `class` causes MSVC to mangle the symbol differently from the
// definition site, producing a "unresolved external" link error.
struct Page;
class PageList;
}  // namespace noted::canvas

namespace noted::ui::widget {

struct NavigatorInputs {
    // Logical canvas dimensions in canvas pixels.
    std::uint32_t canvas_w{0};
    std::uint32_t canvas_h{0};
    // Camera state: world → screen is `screen = canvas * scale +
    // translation`. The visible viewport on the canvas is:
    //   x ∈ [-tx / scale, (window_w - tx) / scale]
    //   y ∈ [-ty / scale, (window_h - ty) / scale]
    double translation_x{0.0};
    double translation_y{0.0};
    double scale{1.0};
    std::uint32_t window_w{0};
    std::uint32_t window_h{0};
};

struct NavigatorResult {
    // When the user clicked / dragged inside the thumbnail, this
    // holds the canvas-space point the camera should re-centre
    // on. The caller applies the resulting translation.
    std::optional<std::pair<double, double>> pan_to_canvas_point{};
};

// Renders the navigator panel as a standard ImGui::Begin window
// — collapsible, resizable, draggable. Same UX as
// `brush_options` / `color_picker_panel`. The pages span is read-
// only metadata used to render the page outlines inside the
// thumbnail; an empty span is allowed (the thumbnail just shows
// the canvas frame).
[[nodiscard]] auto navigator_panel(const NavigatorInputs& in,
                                   std::span<const noted::canvas::Page> pages,
                                   float top_offset_px) -> NavigatorResult;

}  // namespace noted::ui::widget
