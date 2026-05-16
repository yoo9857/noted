# ADR 0014: Canvas render target — two-pass composition

**Status:** Accepted
**Date:** 2026-05-16

## Context

Until now the renderer wrote directly into the swapchain image. That
worked for the textured-quad demo but breaks the moment we add real
features:

- **Note-taking strokes** want to accumulate ink across frames without
  losing it to a clear. A swapchain image is recycled and cleared every
  frame; persistent state can't live there.
- **Layered raster editing** wants to render a stack of layers/filters
  once and composite the result on every frame. Recomputing the entire
  layer DAG every frame would burn GPU time linearly with layer count.
- **Document resolution ≠ window resolution.** A 4K document on a 1080p
  monitor should render once at 4K and downsample at present time.
- **Post-effects** (color correction, vignette, dither) want a single
  fullscreen pass after the layer pipeline, not interleaved with it.

All four motivations converge on the same answer: an **offscreen
"canvas" image** that the document pipeline targets, plus a thin
**composite pass** that draws the canvas onto the swapchain.

## Decision

Introduce `noted::gpu::CanvasRenderTarget` — an RAII wrapper around an
offscreen `gpu::Image` — and `Renderer::render_with_canvas` which
issues two render passes inside a single command buffer per frame.

### Type layout

```cpp
class CanvasRenderTarget {
    static auto create(const Allocator&, const CanvasCreateInfo&)
        -> Result<CanvasRenderTarget>;

    auto resize(const Allocator&, VkExtent2D) -> Result<void>;

    void transition_to(VkCommandBuffer,
                       VkImageLayout,
                       VkAccessFlags2,
                       VkPipelineStageFlags2) noexcept;

    auto color_attachment(VkClearColorValue, /* load_op, store_op */) const
        -> VkRenderingAttachmentInfo;

    auto handle() / view() / format() / extent() / current_layout() ...
};
```

### Image properties

| Property        | Value (v1)                                                        | Why                                                                              |
|-----------------|-------------------------------------------------------------------|----------------------------------------------------------------------------------|
| Format          | `VK_FORMAT_R8G8B8A8_UNORM`                                        | Matches display gamut for the MVP; cheap. Upgrade to `R16G16B16A16_SFLOAT` when HDR/linear-light compositing lands. |
| Usage           | `COLOR_ATTACHMENT_BIT \| SAMPLED_BIT \| TRANSFER_DST_BIT`         | Renderable, sampleable, and accepts direct uploads (thumbnails, snapshots, drag-drop imports). |
| Tiling          | `VK_IMAGE_TILING_OPTIMAL`                                         | Standard. We never read it from CPU.                                             |
| Extent          | Matches swapchain extent (v1)                                     | Document-resolution decoupling lands with [[reference-roadmap]] P4.              |
| Mip levels      | 1                                                                 | Mipmaps add cost and aren't useful for direct compositing.                       |

### Layout tracking

Each `CanvasRenderTarget` instance caches its current
`{layout, access2, stage2}` triple. `transition_to()` emits a
`VkImageMemoryBarrier2` using the cached values as `src*` and the
caller-supplied targets as `dst*`, then updates the cache.

Rationale: avoids forcing the renderer to remember the canvas state
across frames. The first call after `create()` sees
`layout = UNDEFINED, access = 0, stage = TOP_OF_PIPE` — exactly what
Vulkan expects for an initial transition.

`resize()` discards contents (rebuilds the image), so it resets the
state to `UNDEFINED` as well. There is also an explicit
`reset_layout_tracking()` for the (rare) case where the caller
externally wait_idle's and discards.

This is **per-instance**, not a global resource tracker. When we add
multiple canvases (compositor scratch, thumbnails), each tracks itself.
Once we grow a real render graph the tracking moves there and this
local cache becomes redundant — fine, it's an additive optimization.

### Frame structure

`Renderer::render_with_canvas` packs both passes into one command
buffer:

```
waitForFences
acquireNextImage  (→ image_index, picks per-image render_finished)
resetFences
beginCB
  canvas.transition_to(COLOR_ATTACHMENT_OPTIMAL, COLOR_WRITE, COLOR_OUTPUT)
  beginRendering(canvas color_attachment, clear=canvas_pass.clear)
    canvas_pass.draw(cb, canvas.extent())      ← caller records ink/layers
  endRendering
  canvas.transition_to(SHADER_READ_ONLY_OPTIMAL, SAMPLED_READ, FRAGMENT_SHADER)

  swapchain image UNDEFINED → COLOR_ATTACHMENT
  beginRendering(swapchain view, clear=swapchain_pass.clear)
    swapchain_pass.draw(cb, swapchain.extent()) ← caller samples canvas.view()
  endRendering
  swapchain image COLOR_ATTACHMENT → PRESENT_SRC
endCB
queueSubmit2
queuePresent
```

Both passes share the same submission — no extra semaphore is needed
for the canvas→swapchain handoff, just a memory barrier inside the
command buffer. This is the standard sync2 idiom and keeps the
renderer's external interface (acquire/present semaphores) unchanged.

### Resize behavior

When the swapchain resizes, the canvas resizes with it. `resize()` is
*destructive*: contents are discarded and the cached layout is reset
to UNDEFINED. The caller must:

1. `device.wait_idle()` (so no in-flight command buffer references the old image).
2. `swapchain.recreate(...)`.
3. `renderer.rebind_swapchain(...)`.
4. `canvas.resize(allocator, new_extent)`.
5. Rewrite the descriptor that samples `canvas.view()` — the view handle changed.

Step 5 is the easy-to-miss one. `main.cpp`'s `recreate_swapchain`
lambda performs all five.

## Alternatives considered

- **Render strokes directly into the swapchain.** Loses ink between
  frames. Forced a "fully retain & re-replay every frame" model that
  scales poorly past a few thousand strokes. Rejected.
- **Use a `VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT` and merge into one
  subpass.** Tied to legacy subpass dependencies / `VkRenderPass`.
  Conflicts with ADR 0010's "dynamic rendering only" stance. Rejected.
- **R16G16B16A16_SFLOAT canvas now.** Doubles memory + bandwidth before
  we have a use case that needs >8-bit precision. Defer to the
  HDR/linear-light ADR. Rejected for v1, planned for v2.
- **Separate queue / async compute for the canvas pass.** Worth doing
  once GPU-driven layer pipelines exist; premature today.
- **Single render pass with two `vkCmdBeginRendering`-renderArea-suspending
  blocks.** Suspending rendering only matters across CB boundaries.
  Same submission = no benefit. Rejected.

## Consequences

- The renderer now has two top-level entry points: `render_frame_with`
  (single-pass, used by simple demos) and `render_with_canvas`
  (two-pass, used by the real app from this PR onward). The single-pass
  variant stays because it's useful for splash screens, debug overlays,
  and any future render-only-to-swapchain UI.
- Every layer / stroke / filter / image-edit operation in future PRs
  targets `canvas.view()`. The composite pass stays small and
  pipeline-static.
- Format swap (R8 → R16F) is a single line in
  `main.cpp::kCanvasFormat` + two pipeline rebuilds. The canvas type
  doesn't bake the format assumption — `Image::format()` carries it
  through.
- The descriptor that samples the canvas must be re-pointed on every
  resize. We document this in code; future RenderGraph work will make
  view-handle stability a goal.
- One more `Tracy` zone hierarchy: `Renderer::render_with_canvas`
  → `canvas_pass.draw` / `swapchain_pass.draw`. Tracy already shows
  CPU-side stalls; GPU zones land with `TracyVkZone` later (see
  [[ADR-0013]] follow-ups).

## Follow-ups

- **GPU zones** (`TracyVkZone`) labeled per pass.
- **R16F canvas + linear-light blending** ADR.
- **Document-resolution decoupling** — canvas extent independent of
  swapchain extent.
- **Render graph / pass scheduler** — when canvases multiply, fold the
  per-instance layout tracking into a graph.
