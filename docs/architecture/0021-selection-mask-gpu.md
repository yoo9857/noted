# ADR 0021: Selection mask — GPU R8 image + buffer-to-image rasterizer

**Status:** Accepted
**Date:** 2026-05-18

## Context

ADR 0020 shipped `domain::Selection` — the CPU geometric set
(rect-list) used for set algebra, undo / redo, and UI. The Photoshop
side now needs the **other half** of that data: a per-pixel mask the
GPU can sample to gate compositing, stroke deposition, and filter
passes ("operate only inside the selection").

The split mirrors the precedent set by ADR 0016 (LayerGraph domain) →
ADR 0019 (LayerCompositor GPU): data lives in `domain/`, the GPU
consumer lands in `compositor/`. The mask is the third instance of
that pattern.

What the next several PRs need from this layer:

- `LayerCompositor::composite()` takes an optional mask; the layer
  shader multiplies output alpha by the mask sample (next PR, #9c).
- The stroke engine consults the mask before depositing into the
  canvas (lands when stroke + selection wire together).
- Adjustment-layer effects (curves, levels) read the mask in their
  fragment shader.

All three consumers want the same shape: a `VkImageView` they can
bind as a sampled texture with values in [0, 1].

## Decision

Introduce two types:

- **`noted::gpu::SelectionMask`** — `R8_UNORM` image with layout
  tracking, mirroring `CanvasRenderTarget`'s RAII shape.
- **`noted::compositor::SelectionRasterizer`** — owns a
  persistently-mapped staging buffer + records `transition → CPU
  rasterize → vkCmdCopyBufferToImage → transition` onto a caller-
  supplied command buffer.

### Type layout

```cpp
namespace noted::gpu {
struct SelectionMaskCreateInfo {
    VkExtent2D extent;
    // Format pinned at R8_UNORM in v1 — no parameter exposed.
};

class SelectionMask {
    static auto create(const Allocator&, const SelectionMaskCreateInfo&)
        -> Result<SelectionMask>;

    auto resize(const Allocator&, VkExtent2D) -> Result<void>;

    void transition_to(VkCommandBuffer, VkImageLayout,
                       VkAccessFlags2, VkPipelineStageFlags2) noexcept;
    void reset_layout_tracking() noexcept;

    auto handle() / view() / format() / extent() / current_layout() ...
    static constexpr auto bytes_per_texel() noexcept -> uint32_t { return 1; }
};
}

namespace noted::compositor {
void rasterize_to_buffer(const noted::domain::Selection& s,
                         VkExtent2D extent,
                         std::span<uint8_t> dest,
                         uint8_t fill_value = 255) noexcept;

void clear_and_rasterize(...);  // zero-then-rasterize

class SelectionRasterizer {
    static auto create(const gpu::Allocator&, VkExtent2D)
        -> Result<SelectionRasterizer>;
    auto resize_staging(const gpu::Allocator&, VkExtent2D)
        -> Result<void>;
    auto record(VkCommandBuffer, const domain::Selection&,
                gpu::SelectionMask&) -> Result<void>;
};
}
```

### Image properties

| Property        | Value (v1)                                          | Why                                                                              |
|-----------------|-----------------------------------------------------|----------------------------------------------------------------------------------|
| Format          | `VK_FORMAT_R8_UNORM`                                | One byte per pixel, samplable as a normalized [0,1] float. Right size for binary + future feathered masks (8-bit alpha is plenty). |
| Usage           | `SAMPLED_BIT \| TRANSFER_DST_BIT`                   | Shaders read it; the v1 rasterizer writes via `vkCmdCopyBufferToImage`. No `COLOR_ATTACHMENT` until a shader rasterizer lands. |
| Tiling          | `VK_IMAGE_TILING_OPTIMAL`                           | Standard. Never CPU-read.                                                        |
| Extent          | Matches the canvas extent                           | Compositor samples the mask in the same UV space as the canvas.                  |
| Mip levels      | 1                                                   | The mask is sampled at canvas resolution; mipmaps add cost for no benefit.       |

### Layout tracking

Identical pattern to `CanvasRenderTarget` (ADR 0014): each instance
caches its `{layout, access2, stage2}` triple, every `transition_to()`
emits one `VkImageMemoryBarrier2` and updates the cache. Avoids
forcing the renderer to remember the mask's state across frames.

### V1 rasterization: CPU + buffer-to-image copy

The rasterizer iterates the selection's canonical rect list, clips
each rect to the mask bounds, and writes runs of `fill_value` into
the staging buffer with `memset`. A single `vkCmdCopyBufferToImage2`
uploads the result.

Trade-offs that make this the right v1:

- **Marquee selection is the typical case.** 1..N rectangles, one
  update per user gesture. The CPU pass is dominated by clipping
  arithmetic, not the memset, and the memset runs at memory speed.
- **No new shader, no new pipeline.** The rest of the codebase ships
  one new image type and one new buffer; both follow patterns we
  already proved out (Canvas + upload). Total surface for review.
- **Testable.** The rasterizer's pure step (`rasterize_to_buffer`) is
  a free function that takes a `std::span<uint8_t>` — unit tests
  exercise the rect clipping / union / fill semantics without a GPU.
- **The cost ceiling is bounded.** A 4K mask is 16 MB. Even at the
  worst case (full-screen marquee, 60 Hz update from a drag) that's
  ~1 GB/s on the CPU side — well under the budget for non-frame-path
  work. Selections only re-rasterize on user gesture in practice.

The shader-rasterizer route (`select.slang` fragment that writes 1.0
inside each rect via `vkCmdDraw` with a push-constant rect) is the
right call once selections gain polygon / lasso geometry. The class
boundary is set up so consumers see the same `record()` API when the
internal strategy changes.

### Public-API split (data vs. GPU)

| Layer | Lives in | What it does |
|---|---|---|
| `domain::Selection` (ADR 0020) | `domain/` | Pure rect-list set algebra. |
| `gpu::SelectionMask` | `engine/gpu/` | The GPU resource + layout tracking. No knowledge of `domain::Selection`. |
| `compositor::SelectionRasterizer` | `compositor/` | Bridges the two: takes a `domain::Selection`, fills a `gpu::SelectionMask`. |

This is the same shape as LayerGraph (domain) → LayerPayloadStore +
LayerCompositor (compositor). The compositor module is allowed to
depend on both domain and engine; engine has no `domain` dependency.

### Why a separate type rather than reusing `CanvasRenderTarget`

The two images are 95% the same shape, but:

- They have **different formats** (RGBA8 vs R8). CanvasRenderTarget's
  `color_attachment()` helper hardcodes the RGBA semantics; reusing
  it would force a parameter that's only set one way per consumer.
- The mask **does not need COLOR_ATTACHMENT** in v1. Mixing the two
  usage profiles in one type either over-allocates or branches at
  every call site.
- The mask's lifetime is **tied to the canvas extent** but its
  resize policy is simpler — no preserve-contents subtleties because
  the mask is always rebuilt from `domain::Selection` after a
  resize. A separate type makes the contract explicit.

Once a third image type wants to share the layout-tracking
machinery, we factor a `TrackedImage<Format, Usage>` base. Premature
now.

## Alternatives considered

- **Shader-based rasterizer (one draw per rect).** Right answer for
  polygon / lasso. Premature for marquee MVP and forces a new
  pipeline, new shader file, new descriptor wiring. The
  `SelectionRasterizer` class boundary stays so this drops in
  later without breaking callers.
- **`vkCmdClearColorImage` per rect.** Doesn't take a sub-rectangle —
  it clears the whole subresource. Not usable for per-rect fill.
- **Reuse `upload_image_pixels()` directly.** It blocks (one-shot
  fence + wait_idle on upload). Selection re-rasterizes inside the
  frame loop, so we want the recording path to live on the caller's
  command buffer instead of synchronous upload.
- **CSG tree on the GPU (rasterize union directly).** A real GPU
  rasterizer would clip overlapping rects to a disjoint cover or
  rely on max-blend. Not needed: ADR 0020 already canonicalized the
  rect list and the v1 rasterizer's per-rect memset naturally
  computes the union (overlapping memsets write 255 = 255).
- **Half-precision mask (`R16_SFLOAT`).** Premature; the future
  feathered-selection step writes 8-bit alpha and the compositor
  reads as a normalized float regardless.
- **Mask owned by `LayerCompositor`.** Conflates two responsibilities
  (composite + mask materialization) and prevents the stroke engine
  / filters from consuming the same mask. Keep it free-standing.

## Consequences

- New module file in `engine/`: `gpu/selection_mask.{hpp,cpp}`.
- New module file in `compositor/`: `selection_rasterizer.{hpp,cpp}`.
- 11 unit tests in `tests/unit/selection_rasterizer_test.cpp`
  covering: size-mismatch guard, zero extent, single-rect bounds
  (half-open right/bottom), empty selection preserves buffer,
  clipping to mask bounds, fully-outside no-op, overlapping rects
  → union via repeated fill, fill_value override, `clear_and_rasterize`
  zeroing semantics, raster-vs-`Selection::contains` cross-check.
- No changes to `LayerCompositor` yet — the compositor masking PR
  (#9c) does the wiring.
- The mask uses a one-shot CPU staging path; per-frame re-rasterize
  is fine because v1 selections only change on user gesture.

## Follow-ups

- **Compositor masking** (P3 #9c) — `LayerCompositor::composite()`
  takes an optional `SelectionMask&`. Layer shader multiplies output
  alpha by the mask sample.
- **Stroke gating** — `StrokeEngine` consults the mask before
  depositing stamps. Out of scope for #9b/c; lands when stroke +
  selection wire together.
- **Per-frame staging recycling** — once selections start updating
  every frame (e.g. live lasso drag), the rasterizer needs a
  per-frame-in-flight staging ring. v1 leaves this to the caller
  via `wait_idle` because selections only change on gesture.
- **Shader rasterizer + polygon selections** — when lasso /
  polygon geometry lands, the rasterizer grows a draw-based path
  via a `select.slang` fragment shader.
