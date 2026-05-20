# ADR 0031 — Tool state machine + canvas pass strategy for editing tools

**Status:** Accepted (Phase B.1)
**Date:** 2026-05-20

## Context

The product vision (Goodnotes + Photoshop unified canvas) needs the user
to switch between editing tools mid-document: pen for ink, eraser for
mistakes, selection for region operations, shape / text / image for
discrete primitives. The MVP through Phase A.3 had **only the pen** — a
single `noted::stroke::StrokeEngine` instance with a hardcoded
`BrushStyle` and no notion of "which tool is active." Adding more tools
required deciding three things up-front:

1. **Where does "active tool" live?** Per-frame ImGui state? App
   member? Domain object?
2. **How does the stroke engine learn what to draw?** Per-stroke flag?
   Per-stroke pipeline switch? Per-tool engine?
3. **How does eraser actually erase without destroying the page
   background?** The canvas pass today layers `PageRenderer → LayerCompositor →
   StrokeEngine` into a single render target — a "paint with background
   colour" eraser would also cover the grid / lined / dotted pattern.

These three are entangled. Solving them as one Phase B foundation
decision lets the follow-up tool-specific PRs (eraser, colour picker,
selection rectangle, …) target a stable surface instead of each one
re-litigating the framework.

## Decision

### Tool state lives in `domain::tool::ToolState`

`noted::domain::tool::ToolKind` is a wire-stable enum naming **all six
tools the product will ship** — `pen, eraser, select, shape, text,
image` — even though only the first two have behaviour today. Naming
them up-front pins the ordinals (`pen=0` → `image=5`) so `.noted` v3
(which will persist the active tool) doesn't need a follow-up
enum-reorder. Adding a seventh tool appends.

`ToolState` is a POD holding the active kind. **Per-tool option
payloads** (brush size / hardness / opacity for pen, hardness /
preserve-alpha for eraser, shape kind / fill / stroke for shape, font /
size for text, …) land alongside their behavioural integration. The
POD is move-only-friendly and serialisable; equality is defaulted.

The `ToolState` lives **on the App** for v0.x (not on `Document`)
because tool choice is a session-local preference like camera zoom, not
document content. Phase 4 of the AAA cleanup (per
[ADR 0030](0030-runtime-config.md)) may eventually persist the last-
used tool in per-user settings; until then it resets to Pen on every
launch.

### `brush_for_tool(kind) -> BrushStyle` is the integration seam

Phase B.1 maps each tool to a `noted::stroke::BrushStyle`:

| Tool   | Brush                                                              |
| ------ | ------------------------------------------------------------------ |
| Pen    | Default (black, 2..10 px radius, gamma 1.8)                        |
| Eraser | Paper colour (245/243/235 RGB, matches `kPaper` in `page_bg.slang`) |
| Others | Pen — until their behavioural integration lands                    |

The widget palette emits a `switch_request`; the App applies it to
`tools_.active` and calls `stroke_engine_->set_brush(brush_for_tool(...))`.
Because `set_brush()` only affects future presses (per
[ADR 0018](0018-stroke-engine-pressure.md)), in-flight strokes are
unaffected — the swap is non-destructive.

This indirection — a free function in `app.cpp` rather than a
`ToolState::brush()` method — keeps the domain layer free of
engine-specific types. The mapping is a presentation concern that
belongs alongside the app's other UI wiring.

### Eraser semantics: Phase B.1 placeholder → Phase B.2 canvas pass split

The Phase B.1 eraser is **"paint with paper colour"** — a deliberate
placeholder. The current canvas pass does:

```
clear canvas → PageRenderer → LayerCompositor → StrokeEngine
```

An eraser stroke painted with paper colour visually merges with blank
backgrounds but **covers the grid / lined / dotted pattern** because
the strokes overdraw whatever was rendered below. This is acceptable
for v0.x development on plain pages and unacceptable for the product.

**Phase B.2 will restructure the canvas pass** into a layered scheme:

```
canvas:          clear → PageRenderer → LayerCompositor      (paper + layers)
strokes_target:  clear (transparent) → StrokeEngine          (ink only)
canvas:          composite(strokes_target) with SRC_OVER     (overlay)
```

With strokes in a separate RGBA target, eraser strokes use **destination-out**
blending (`srcAlphaBlendFactor = ZERO`, `dstAlphaBlendFactor =
ONE_MINUS_SRC_ALPHA`) which clears alpha in the strokes target without
touching the canvas. When the strokes target is overlaid back onto the
canvas, the page pattern shows through the erased region — correct
Goodnotes behaviour.

This restructure also gives later phases their natural seams:

- **Per-stroke vector erase** (Illustrator-style — clipping ink polylines
  with eraser polylines) becomes a data-side transform that produces a
  smaller strokes target without changing the GPU code.
- **Raster brushes** (the Photoshop side) write into the same strokes
  target with arbitrary RGBA, then participate in the same overlay.
- **Per-layer ink** (eventually: ink that lives on a specific
  `LayerGraph` node rather than the canvas root) becomes a per-layer
  strokes target the compositor consumes.

The strokes-target requires a new `gpu::StrokeTarget` analogous to
`gpu::CanvasRenderTarget` ([ADR 0014](0014-canvas-render-target.md)) —
same layout-tracking pattern, R8G8B8A8 format, COLOR_ATTACHMENT |
SAMPLED | TRANSFER_DST. The composite pipeline gains a "stroke overlay"
variant that samples the strokes target with normal alpha blend.

### Per-tool option payloads land with their tool's behaviour PR

Pen options (brush size, hardness, opacity, colour) land in **Phase B.3
(Brush options + colour picker)** as a `PenOptions` payload bound to
the active `ToolState`. The pen tool reads its options through a
`tools_.pen_options` field; the palette widget renders the option
sliders below the tool buttons when Pen is active.

Eraser options (hardness, size, preserve-alpha) land in **Phase B.2**
alongside the canvas pass split — the option struct gives the eraser
a runtime-tweakable footprint.

The remaining four tools (`select`, `shape`, `text`, `image`) gain
their option payloads when their behavioural integration lands.

## Alternatives considered

- **One engine per tool** (PenEngine, EraserEngine, SelectionEngine,
  …). Rejected: every engine needs the same input plumbing
  (PointerPressed / PointerMoved / PointerReleased subscriptions, view
  transform sync, canvas extent). Five copies of that is the worst
  kind of duplication. The shared engine + per-tool brush is the same
  pattern Photoshop uses internally — one stamp pipeline, multiple
  configurations.
- **Per-stroke flag on `Stroke` for eraser-ness, branch in shader.**
  Rejected for B.2 timing: the canvas pass restructure delivers the
  RIGHT semantics (page pattern preserved through erasures); a shader
  branch can't fix that because the strokes are already mixed into the
  canvas by the time the shader runs.
- **Reverse-subtract blend on the existing canvas target.** Rejected:
  same root cause. The canvas mixes pages + layers + strokes; subtracting
  removes pixels from all three, not just the ink.
- **Vector-only eraser** (Illustrator-style, no raster erase). Rejected
  as the primary path: the Photoshop side needs raster brushes (image
  edits, photo retouching) and they don't have polyline geometry to
  clip. The canvas pass split supports both — vector strokes can still
  be clipped at the data layer if a future PR adds that, while raster
  brushes go through the same strokes target.
- **Persist `ToolState` in `Document`** (so opening a document restores
  the last tool used). Rejected for v0.x: tool choice is a session
  preference, not document content. Re-evaluate once the Preferences
  UI lands (AAA Phase 4 per [ADR 0030](0030-runtime-config.md)).

## Consequences

- **Phase B.1 (this PR — #69) ships:** `noted::domain::tool::{ToolKind,
  ToolState, label}`, `noted::ui::widget::tool_palette`, `brush_for_tool`
  mapping in `app/src/app.cpp`. The eraser is a paper-colour placeholder;
  the framework is the deliverable, not the eraser.
- **Phase B.2 (next PR):** `gpu::StrokeTarget`, canvas pass split, real
  destination-out eraser. The strokes target also unblocks per-layer
  ink and raster brushes (future phases).
- **Phase B.3 (next):** Per-tool option payloads (brush size /
  hardness / opacity / colour), colour-picker widget, palette layout
  refresh.
- **Phase B.4+ (multiple PRs):** Selection tool (rectangle / lasso),
  shape tool, text tool, image tool — each gains a behavioural PR with
  its option payload.
- **`.noted` v3 (eventually):** Persist `ToolState` per-document or
  per-session. The wire-stable ordinals from B.1 make this a strict
  schema add.

## References

- [ADR 0014 — Canvas render target — two-pass composition](0014-canvas-render-target.md)
- [ADR 0015 — Stroke engine MVP](0015-stroke-engine-mvp.md)
- [ADR 0018 — Stroke engine pressure](0018-stroke-engine-pressure.md)
- [ADR 0019 — Layer compositor](0019-layer-compositor.md)
- [ADR 0027 — UI stack selection (Dear ImGui)](0027-ui-stack-selection.md)
- [ADR 0029 — Vector ink polyline](0029-vector-ink-polyline.md)
- [ADR 0030 — Runtime config](0030-runtime-config.md)
