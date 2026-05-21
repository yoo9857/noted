# ADR 0033 — Page-on-desk visual model

**Status:** Accepted. Implementation lands in `feat/desk-bg`.
**Date:** 2026-05-21

## Context

The product vision is a "Goodnotes-surpassing notes + Photoshop-
surpassing raster editor in one unified app." Through Phase B.7, the
visual rendering accreted as follows:

- `kCanvasClear = (0, 0, 0, 1)` — the offscreen canvas target clears
  to opaque black; pages render on top as paper-coloured rectangles.
- `kSwapchainClear = (0.05, 0.05, 0.10, 1)` — the final swapchain
  clears to dark teal as a regression tell for any frame in which
  the composite quad failed to cover the swapchain (see
  `feat/composite-quad-fix`, PR #67).
- The composite pass uses a 6-vertex quad sized to the canvas
  extent; under camera zoom < 1 the quad shrinks inside the
  swapchain, so the dark teal becomes visible as a margin.

User feedback during B.7.b.2 acceptance testing flagged that the
result reads as "computer graphics" rather than "paper on a desk":

1. Pages float against pure black inside the canvas, with no
   indication that they're physical objects.
2. Under zoom out, a dark teal border appears around the canvas
   margin — a third, jarring colour.
3. The page rectangle has no shadow or border; its only visual
   anchor is the abrupt black-to-paper transition.

Goodnotes / Notability / Procreate all share a convention that
informs the fix:

- A single neutral "desk" colour fills the viewport everywhere
  outside a page.
- Each page carries a soft drop shadow so it reads as a real
  piece of paper on the desk.
- No hard "canvas edge" is ever visible to the user; pages float
  in (apparently) infinite space.

## Decision

Adopt a two-slice approach: ship the **visual feel** of the
page-on-desk model now (this ADR's scope), and defer the
architectural follow-up that makes the workspace truly infinite to
a separate ADR.

### Slice 1 — visual model (this ADR)

1. **Unified desk colour.** `kCanvasClear` and `kSwapchainClear` are
   set to the same RGB — a dark neutral grey (`0.13, 0.13, 0.15`).
   The visible background is now uniform regardless of zoom level,
   eliminating the canvas-vs-swapchain two-tone seam.

2. **Page drop shadow.** `shaders/page_bg.slang` emits a quad
   `kShadowMargin = 24 px` larger than the page rect on every side.
   The fragment shader classifies each pixel as page-body (full
   alpha paper + pattern, unchanged) or shadow apron (semi-
   transparent black with a quadratic falloff to zero alpha at the
   apron's outer edge, peak `kShadowOpacity = 0.45`).

3. **Alpha blending on the page pipeline.** `PageRenderer`'s pipeline
   switches from `blendEnable = VK_FALSE` to standard straight-alpha
   source-over. The page body still writes alpha = 1 (overwrite),
   so the only blended pixels are the shadow apron.

The previous "regression tell" role of the swapchain clear (the
dark teal) is no longer needed: composite-pass coverage is now
exercised in CI via the smoke run and by every PR's end-to-end
test; future regressions would surface immediately as visible
desk grey instead of a startling colour, which is the right
default.

### Slice 2 — true infinite workspace (deferred to ADR 0034)

The current implementation keeps the offscreen `CanvasRenderTarget`
sized to the swapchain extent. Pages render into the offscreen
target at canvas-pixel coordinates; the composite quad then
transforms the entire target via the camera into screen space.
This means:

- The "world" is bounded by the canvas extent — pan past it and
  pages get clipped against the texture edge.
- A 1080 × 1920 swapchain can't simultaneously show two A4 pages at
  zoom = 1 because the offscreen target isn't large enough.

Slice 2 will skip the offscreen target entirely (or grow it to
encompass the full page stack with a generous margin). Page and
stroke draws will write directly to the swapchain via a camera-
transformed vertex shader. This is a larger change touching every
shader that currently assumes canvas-pixel space and the entire
render-pass orchestration; it ships as its own ADR + PR pair.

Slice 1 alone gets the visual rhythm right at typical zoom
levels (≥ 0.5×) where the existing canvas extent is large enough
to cover the visible pages; Slice 2 removes the cap entirely.

## Alternatives considered

- **Page border instead of shadow.** Cheaper but reads less like
  a physical object — a sharp line against grey looks like a UI
  card, not paper. Rejected.

- **Background image / paper texture.** Higher polish ceiling but
  pulls in an asset dependency and complicates colour-managed
  rendering (the texture has to live in the same colour space as
  the desk clear). Defer to a theme system pass.

- **Light-theme desk colour.** A bright off-white desk reads well
  with Goodnotes' style but fights the rest of the in-app UI which
  is currently dark-themed. Theme switching (ADR 0027's
  `noted::ui::theme::apply`) will eventually drive both UI and
  desk colour from a single palette; for now, dark desk only.

- **Shadow as a separate compositor pass.** Cleanest separation
  but doubles the page draw count + adds a pipeline. The
  apron-in-shader approach costs one extra geometry layer per
  page (still 6 verts) and ~25% more fragment work per page
  edge — well below any frame budget concern.

## Consequences

- Visual feel of the app is decisively closer to Goodnotes /
  Procreate / Notability at every zoom level.
- The "dark teal regression tell" is gone. Any future composite
  miss now blends with desk grey rather than screaming a different
  colour. Acceptable trade-off because CI and smoke runs exercise
  the path.
- `PageRenderer` is now alpha-blending. If a future pass relies
  on the page being the opaque base layer (it shouldn't, but the
  old comment claimed it), the assumption is now wrong; ADR 0033
  is the canonical reference.
- Slice 2 (ADR 0034) needs to land before pages can be panned to
  the screen's edge without clipping — a Goodnotes-grade scrolling
  notebook feel requires it.
