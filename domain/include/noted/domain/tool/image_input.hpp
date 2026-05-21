#pragma once

// Image-tool data — pure data + clamping.
//
// Phase B.7 of the unified-canvas plan. The Image tool's interaction
// model is **click + place**: a single press anchors the top-left of
// an image primitive at the canvas point, using the size + tint
// configured in `ImageOptions`.
//
// v0.x scope (this PR):
//   - `ImageOptions` — width, height, RGBA tint. Bound to UI sliders /
//     colour-picker on `ToolState::image`. Defaults to a 200×200
//     opaque-white "placeholder" — visible against the page pattern.
//   - `ImagePrimitive` — committed image. Position + dimensions +
//     tint colour, snapshotted from `ImageOptions` at press time.
//     Same discipline as Stroke / ShapePrimitive / TextPrimitive
//     (mid-action slider tweaks never repaint history).
//   - Rendering: a coloured rectangle + dashed border + "Image"
//     label, drawn by `ui/widget/image_overlay`. Acts as a stand-in
//     for a real raster — the file-picker + decode + GPU upload
//     lands in B.7.b.
//
// What's NOT here (deliberate, deferred to B.7.b):
//   - File picker (nativefiledialog-extended is already a dep —
//     plumbed in the follow-up).
//   - stb_image decode + Vulkan image upload.
//   - `ImTextureID` registry + ImGui's vulkan-backend
//     `ImGui_ImplVulkan_AddTexture` lifecycle.
//   - Per-image rotation / shear / crop — once a real image is
//     loaded, these become non-trivial; punted until then.
//
// Persistence: `Document::images()` owns the primitive vector; mutation
// via `Add/RemoveImageCommand` (PR #88). B.7.b.1 extends every primitive
// with an `AssetId` that resolves into `Document::image_assets()` — the
// raster bytes themselves arrive in B.7.b.2 (file picker + decode) and
// land in the `.noted` zip alongside `document.json` in B.7.b.3.

#include <cstdint>

#include "noted/domain/document/asset_id.hpp"

namespace noted::domain::tool {

// User-tweakable image-placement parameters. Lives on
// `ToolState::image`; the `brush_options` widget renders size
// sliders + a tint colour picker that mutates these in place.
// Snapshotted into `ImagePrimitive` at press time.
struct ImageOptions {
    // Pixel dimensions of the placeholder. 200×200 is large enough
    // to be obvious on the canvas at 1× zoom, small enough not to
    // cover the whole page on a fresh click. The slider exposes
    // 8…4096 px — anything beyond that is a deliberate authoring
    // choice and can be edited via `Document::images()` once
    // persistence lands.
    float width_px{200.0F};
    float height_px{200.0F};

    // Straight-alpha tint. Default = opaque white, so the placeholder
    // rectangle is clearly visible against any page background. Once
    // real images load (B.7.b), this becomes a multiplicative tint
    // applied in the fragment shader.
    float r{1.0F};
    float g{1.0F};
    float b{1.0F};
    float a{1.0F};

    [[nodiscard]] auto operator==(const ImageOptions&) const noexcept -> bool = default;
};

// One committed image — what `image_overlay` renders + what
// `.noted` v6 persists. Value type; cheap to store in a `std::vector`.
struct ImagePrimitive {
    // Top-left anchor in canvas pixels.
    double x{0.0};
    double y{0.0};

    // Dimensions in canvas pixels. Always ≥ 1 (clamped by
    // `image_primitive_from`). Negative / NaN / zero collapse to 1.
    float width_px{200.0F};
    float height_px{200.0F};

    // Tint colour snapshotted from `ImageOptions` at commit time.
    // Carried per-primitive so a mid-document slider tweak doesn't
    // repaint history.
    float r{1.0F};
    float g{1.0F};
    float b{1.0F};
    float a{1.0F};

    // Handle into `Document::image_assets()`. `invalid_asset_id` (= 0)
    // means "placeholder, no decoded bitmap yet" — the v0.x render
    // path falls back to the dashed-border + label drawing. B.7.b.2
    // populates this field when the file picker successfully decodes
    // an image; B.7.b.3 round-trips the asset bytes through the
    // `.noted` zip keyed by this id.
    noted::domain::AssetId asset_id{noted::domain::invalid_asset_id};

    [[nodiscard]] auto is_degenerate() const noexcept -> bool {
        return width_px < 1.0F || height_px < 1.0F;
    }

    [[nodiscard]] auto operator==(const ImagePrimitive&) const noexcept -> bool = default;
};

// Build an `ImagePrimitive` from a commit position + the snapshotted
// options + an optional asset id. Width/height clamped to a 1 px
// floor; NaN / negative collapse to 1.
//
// `asset_id` defaults to `invalid_asset_id` so the placeholder
// commit path (no file picker yet) keeps working unchanged.
//
// Position is the **top-left** anchor — same convention as
// `TextPrimitive` and matches ImGui's draw-list rect API.
[[nodiscard]] auto image_primitive_from(
    double x,
    double y,
    const ImageOptions& opt,
    noted::domain::AssetId asset_id = noted::domain::invalid_asset_id) noexcept -> ImagePrimitive;

}  // namespace noted::domain::tool
