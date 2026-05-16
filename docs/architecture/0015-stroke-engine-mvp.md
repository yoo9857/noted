# ADR 0015: Stroke engine — MVP disk-stamp pipeline

**Status:** Accepted
**Date:** 2026-05-16

## Context

ADR 0014 introduced the canvas render target. This ADR specifies how
input becomes pixels — the first MVP of the stroke engine. The bar is
deliberately low ("crude but proves the input-→-pixel path", per
HANDOFF P2 #4); the precedent set here, however, is what every future
stroke / brush / vector-ink feature will extend.

Three concerns the design has to settle:

1. **What shape is a stamp?** A real brush is anti-aliased; pixel-art
   nearest-neighbor disks look bad even as a placeholder.
2. **How does a static `StrokeEngine` subscribe to event channels
   safely?** The `Channel::subscribe` API takes a `std::function`; if a
   subscribed lambda captures `this`, moving the StrokeEngine
   invalidates the captured pointer.
3. **Per-frame vs incremental rendering.** Re-rendering every stamp
   every frame is O(strokes); incremental rendering needs `LOAD_OP_LOAD`
   plumbing and a "what's new this frame" partition.

## Decision

### Stamp shape — SDF disk in `stamp.slang`

A single Slang file with two entries:

- `vs_stamp` — given `SV_VertexID ∈ [0,6)` and a push-constant
  `{center, radius}`, emits a 6-vertex (two-triangle) quad in NDC that
  bounds the disk.
- `ps_stamp` — signed-distance-to-circle in pixels; `smoothstep` over
  a `softness_px` band produces the anti-aliased edge; outputs straight
  RGBA color modulated by the SDF.

Pipeline state:

- `topology = TRIANGLE_LIST`, `cull = NONE`, no vertex buffer (vertices
  are synthesized from `SV_VertexID`).
- Color blend: `SRC_ALPHA / ONE_MINUS_SRC_ALPHA` for color,
  `ONE / ONE_MINUS_SRC_ALPHA` for alpha. Straight-alpha input, correct
  composition.
- Push constant: `StampPush { vec4 color; vec2 canvas_size; vec2
  center_px; float radius_px; float softness_px; }` — 40 bytes,
  std430-clean (vec4 → vec2 → scalar). A `static_assert` in
  stroke_engine.cpp pins the C++ POD's layout to the same 40 bytes.

### Lifetime — heap-allocated, non-movable engine

`StrokeEngine::create` returns `Result<std::unique_ptr<StrokeEngine>>`.
The engine is **non-movable**: its address IS its identity. Subscription
lambdas capture `this` (resolved into `self` raw pointer) and rely on
that address never shifting.

This trade — heap allocation for stable identity — is the cleanest
way to express the subscription invariant in C++:

- Movable types can't safely register `this`-capturing callbacks
  unless every move also re-subscribes (error-prone).
- Stack-allocating and forcing the caller to never copy/move works
  but is fragile (NRVO is not guaranteed for `return local;`).
- Reference-counted (`shared_ptr<Impl>` + `weak_ptr` in lambdas)
  invites lifecycle bugs around hook firing on a dead engine.

A `TestingTag` ctor builds a no-pipeline / no-subscription engine for
unit tests, which exercise the same `on_pressed` / `on_moved` /
`on_released` / `on_resized` methods via `inject_*_` helpers. The
test path doesn't touch the GPU.

### Rendering — re-record every frame from the stamp list

Every frame, the canvas pass clears (LOAD_OP_CLEAR) the canvas, draws
the textured background, then asks the stroke engine to record every
accumulated stamp. The cost is one `vkCmdPushConstants + vkCmdDraw(6)`
per stamp.

That's O(strokes) per frame. For 2026-hardware-baseline integrated
GPUs that's ~50k stamps before we run out of frame budget, well past
what a 6h MVP demo needs.

The structurally correct fix — `LOAD_OP_LOAD` + only-new-stamps —
lands with the **layer compositor** (P3 #8). Reasons it's not done
here:

- The compositor will own the canvas's load/store policy; baking
  `LOAD_OP_LOAD` into the renderer now means undoing it later.
- The compositor's "what's new this frame" partition is the right
  place for an incremental render delta, not the stroke engine.
- The 50k-stamp ceiling is fine for the period between this PR and
  feat/layer-compositor landing.

### Coordinate system

Pixel space, top-left origin. Matches GLFW pointer events directly:
no flip, no scaling, no off-by-one. The vertex shader does
`ndc = pixel / canvas_size * 2.0 - 1.0`. Sized by either:

- `set_canvas_size(extent)` — called explicitly by main.cpp after
  swapchain creation.
- `FramebufferResized` hook — auto-updates on window resize.

Canvas pixel-size and framebuffer pixel-size are equal in v1 (canvas
matches swapchain). When document-resolution decoupling lands (P4),
the stroke engine takes its size from the canvas, not the framebuffer.

### Brush parameters

Hardcoded MVP brush: `radius_px = 4`, `softness_px = 1`, opaque
black. Pressure scales alpha (`a *= pressure`) — pointless today
because GLFW always reports `pressure = 1`, but in place for when
[[reference-roadmap]] P2 #5 (`feat/pen-input`) lands and real pressure
values arrive.

Custom brushes (`feat/stroke-engine-pressure`, P2 #6) replace the
`kDefault*` constants with a `BrushStyle` parameter and add per-stamp
size/opacity curves driven by pressure.

## Alternatives considered

- **Triangle mesh per disk** (16-segment tessellation): higher vertex
  count, no anti-aliasing without MSAA. SDF wins on both.
- **Geometry / mesh shaders**: cleaner for large stamp batches, but
  ADR 0011 calls them out as post-MVP. The MVP draw loop is fine.
- **Instanced single quad** + per-instance attribute buffer: would
  fold the per-stamp loop into one `vkCmdDrawInstanced`. Better at
  scale, but requires a buffer-management story (resize when stamp
  count grows, GPU-visibility coherency, fence-aware reuse). Worth
  it for the compositor; overkill for MVP.
- **Path-based rendering** (Bezier strokes → triangulated path):
  the right model for vector ink, but a much larger PR and tied to
  ADR-15 (UI stack) for the brush settings UI. Stamps are the
  pragmatic first move; paths land in a later ADR.
- **Compute-shader rasterizer**: AAA-grade endgame but only
  meaningful once we have GPU-driven dispatch. Premature.

## Consequences

- The codebase now has a `noted::stroke` namespace and its own module
  area (`engine/include/noted/engine/stroke/`, `engine/src/stroke/`).
  Future stroke work — pressure mapping, smoothing, path
  recognition — goes here, not in `gpu/`.
- The hook system's RAII Subscription wrappers get their first
  in-production use (the renderer doesn't use them yet). The pattern
  is now documented by example.
- `noted_unit_tests` gains 8 new tests covering the
  press/move/release state machine, including overlapping-button
  edge cases. They run without a GPU and don't add CI time noticeably.
- `stamp.vs_stamp.spv` and `stamp.ps_stamp.spv` join the shader
  output set. The slang compile target depends on both.
- `main.cpp`'s canvas_draw callback grew from "one pipeline, one
  draw" to "two pipelines, one draw + one stamp loop". The
  multi-pipeline-in-one-pass pattern is now established.

## Follow-ups

- `feat/pen-input` (P2 #5) — real pressure values feed `stamp.a`.
- `feat/stroke-engine-pressure` (P2 #6) — pressure-driven `radius_px`
  + per-stroke smoothing.
- `feat/layer-compositor` (P3 #8) — incremental render path,
  `LOAD_OP_LOAD` plumbing, retires the O(strokes) per-frame cost.
- Tracy GPU zones (`TracyVkZone`) around `StrokeEngine::record` —
  pairs with the broader VkZone follow-up from ADR 0013.
- Undo/redo — `StrokeEngine::clear_stamps` + `Command::apply()`
  hook into the [[feedback-workflow]]-defined command stack
  (P4 #11).
