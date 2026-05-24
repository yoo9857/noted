#pragma once

// Image overlay — renders committed images on top of the canvas.
//
// Phase B.7. Two render paths share the overlay:
//   1. **Textured path (B.7.b.2b)** — when `texture_lookup` returns a
//      non-null handle for the primitive's `asset_id`, draw the
//      sampled texture via ImGui's `AddImage` with the per-primitive
//      tint applied at the draw-list level.
//   2. **Placeholder path (legacy / fallback)** — for primitives
//      whose asset hasn't been uploaded yet (decode in flight,
//      decode failed, asset_id == invalid_asset_id, or v9-era loaded
//      file that didn't bundle bytes), draw the dashed
//      placeholder + "Image" label so the primitive still has
//      visible presence.
//
// Pure-domain API — overlay sees only `domain::tool` types + two
// callbacks (canvas-to-screen projection + texture lookup). Same
// boundary discipline established in B.6 (`text_overlay`): the ui
// layer must never include from `app/`.

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "noted/domain/document/asset_id.hpp"

namespace noted::domain::tool {
struct ImagePrimitive;
}  // namespace noted::domain::tool

namespace noted::ui::widget {

// Canvas-pixel (x, y) → screen-pixel (x, y). Same callback shape as
// the other overlays.
using ImageCanvasToScreenFn = std::function<std::pair<float, float>(double, double)>;

// AssetId → ImGui-visible texture handle. Returns 0 / null to
// indicate "no GPU texture for this asset yet — please draw the
// placeholder." The return type is `std::uint64_t` because
// `ImTextureID` is `ImU64` in the docking branch we ship, and we
// don't want this header to include `<imgui.h>` (it would force
// every TU that touches an `ImagePrimitive` vector to pull in
// imgui). The overlay impl casts to `ImTextureID` at the draw call.
using ImageTextureLookupFn = std::function<std::uint64_t(noted::domain::AssetId)>;

// Render committed images. `enabled` lets the host hide the overlay
// (View → Image overlay menu toggle) without touching the underlying
// vector — same UX as the other overlay panels. `texture_lookup`
// may be empty (no callable assigned), in which case every primitive
// falls back to the placeholder draw.
void image_overlay(const std::vector<noted::domain::tool::ImagePrimitive>& images,
                   const ImageCanvasToScreenFn& canvas_to_screen,
                   const ImageTextureLookupFn& texture_lookup,
                   bool enabled);

}  // namespace noted::ui::widget
