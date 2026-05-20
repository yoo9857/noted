#pragma once

// Image overlay — renders committed image placeholders on top of the
// canvas.
//
// Phase B.7. v0.x scope: each `ImagePrimitive` draws as a coloured
// rectangle + dashed border + "Image" label, acting as a stand-in
// for a real raster. The full upload path (stb_image decode + VMA
// allocation + `ImTextureID` registry) lands in B.7.b; the data
// schema + handler + overlay scaffold here keeps the architecture
// honest while the GPU plumbing is built.
//
// Pure-domain API — overlay sees only `domain::tool` types + a
// canvas-to-screen projection callback. Same boundary discipline
// established in B.6 (`text_overlay`): the ui layer must never
// include from `app/`.

#include <functional>
#include <utility>
#include <vector>

namespace noted::domain::tool {
struct ImagePrimitive;
}  // namespace noted::domain::tool

namespace noted::ui::widget {

// Canvas-pixel (x, y) → screen-pixel (x, y). Same callback shape as
// the other overlays.
using ImageCanvasToScreenFn = std::function<std::pair<float, float>(double, double)>;

// Render committed images. `enabled` lets the host hide the overlay
// (View → Image overlay menu toggle) without touching the underlying
// vector — same UX as the other overlay panels.
void image_overlay(const std::vector<noted::domain::tool::ImagePrimitive>& images,
                   const ImageCanvasToScreenFn& canvas_to_screen,
                   bool enabled);

}  // namespace noted::ui::widget
