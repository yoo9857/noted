# 0029 — Vector ink: polyline ribbon rendering

**Status:** Accepted · **Date:** 2026-05-19 · **Supersedes:**
[0015 — Stroke engine MVP (SDF disk stamps)](0015-stroke-engine-mvp.md)
for the rendering side. [0018](0018-stroke-engine-pressure.md) still
applies — the pressure curve (`BrushStyle` + `stamp_from_pressure`)
is the single source of truth for per-sample width and is reused
inside the ribbon tessellator.

## Context

ADR 0015 chose **SDF disk stamps** (one anti-aliased quad per pen
sample, fragment shader computes signed-distance + alpha) as the
v0.x ink representation. That model shipped end-to-end and proved
the input → canvas pipeline, but two limits forced the migration:

1. **Stamps live at the resolution they were drawn at.** Once
   PR #57 added pan + zoom, zooming past 1.0× revealed that
   stamps are baked rasters — they blur out. Goodnotes /
   Procreate / Concepts all use a centerline-plus-ribbon model
   because that's the only way to stay crisp at any zoom.
2. **No file-format roundtrip fidelity.** The `.noted` format
   (PRs #35 / #37) can carry pen samples losslessly, but only
   if the in-memory model IS those samples. Stamps lose the
   centerline + pressure on the way to the canvas.

A third reason — caching: tessellation is a pure function of
(centerline, brush style) so per-stroke caching slots in without
re-deriving anything when nothing changed. PR-after-next territory,
but the current shape makes it cheap.

## Decision

**Centerline + GPU-tessellated triangle-strip ribbon**, replacing
the SDF disk pipeline wholesale per ADR 0012's no-half-migrations
rule. Three landed PRs cover the migration:

- **A.2.a (PR #59)** — pure-logic `Stroke` / `StrokeSample` /
  `RibbonVertex` types + `tessellate_ribbon` in
  `engine/include/noted/engine/stroke/stroke_geometry.hpp`. 10
  unit tests pin the math: empty / single / duplicate degenerate
  cases, perpendicular-to-tangent invariant, pressure-ramp width
  monotonicity, right-angle turn averaging.
- **A.2.b (PR #60)** — `StrokeEngine` swaps its internal storage
  from `vector<Stamp>` to `vector<Stroke>` (+ in-flight
  `current_stroke_`). Rendering still went through the SDF
  stamp pipeline by tessellating each sample to a stamp on the
  fly — visually identical, internal model upgraded, 234 tests
  green. Two key behaviour changes shipped with this:
    - Stroke style is **snapshotted at press time**. Mid-stroke
      brush edits do not repaint accumulated ink.
    - Single-sample strokes (press + release with no movement)
      are **dropped on release**. They have nothing for the
      ribbon to render.
- **A.2.c (this PR)** — the actual GPU swap:
    - New `shaders/polyline.slang` — vertex input
      `pos:float2` + `col:float4` (matches `RibbonVertex`'s
      24-byte layout), 8-byte `PolylinePush` carrying canvas
      size for the pixel → NDC conversion, straight passthrough
      fragment with the pipeline's SRC_ALPHA blend doing the
      compositing.
    - `StrokeEngine::create()` requires a `noted::gpu::Allocator`
      and builds a persistently-mapped 64 KiB `HOST_VISIBLE`+
      `HOST_COHERENT` vertex buffer alongside the new
      triangle-strip pipeline (vertex input bindings declared
      via the existing `GraphicsPipelineBuilder::vertex_input`).
    - `StrokeEngine::record()` now does a three-pass dance:
      tessellate every stroke into a CPU-side vector, grow the
      ribbon buffer geometrically if the frame's bytes overflow
      capacity (capped at 16 MiB), memcpy the entire ribbon
      into the mapped pointer, then bind once and issue one
      `vkCmdDraw(firstVertex, count)` per stroke.
    - `shaders/stamp.slang` is **deleted** — no half migration.

## Alternatives

1. **Keep stamps + supersample on zoom.** Cheap to ship, but
   never as crisp as vector. A 4× zoom on a 4× supersampled
   stamp is still a 1×-equivalent image stretched 4×. Doesn't
   solve the file-format problem either.
2. **Per-stroke vertex buffer.** One `VkBuffer` per `Stroke`,
   uploaded on commit, never re-tessellated. Lower per-frame
   CPU. Rejected for v0.x: at typical stroke counts (≤ a few
   hundred per document) per-frame tessellation costs less than
   the buffer-creation overhead of the alternative. The
   per-stroke option is also strictly additive — we can
   introduce it later under the existing `Stroke` model with
   no shader or API changes.
3. **GPU-side tessellation via a mesh shader.**
   `VK_EXT_mesh_shader` from ADR 0011's P5 roadmap is the right
   long-term home. Deferred — the CPU path is plenty for v0.x
   and the mesh-shader path needs the polyline shape to be
   stable first.
4. **Push-constant variable-width line (no quad expansion).**
   Lines are `VK_PRIMITIVE_TOPOLOGY_LINE_STRIP` with
   `VkPipelineRasterizationLineStateCreateInfoKHR` for width.
   Vulkan line width support is broken on many drivers (D3D12
   parity says 1 px); not portable.

## Consequences

- `StrokeEngineCreateInfo` now requires
  `allocator: const Allocator*` alongside the existing fields.
  Callers (`App::init_stroke_engine`) plumb the App's
  `noted::gpu::Allocator`.
- Buffer growth is geometric (double) with a 16 MiB ceiling.
  Beyond the ceiling the engine drops the tail rather than
  failing — pragmatic UX (the user keeps drawing, ink stops
  appearing for the last few samples) and the cap is generous
  enough that hitting it is a programming bug worth a
  follow-up fix anyway. Resize happens without an explicit
  `device->wait_idle()`; rationale + acknowledged limitation
  documented in the `record()` source comments.
- `stamp.slang` + the `vs_stamp` / `ps_stamp` shader outputs
  are gone. Anyone re-using them must port to the polyline
  layout.
- Visual output: identical at 1× zoom (stamps and short ribbons
  with `min_radius_px == max_radius_px` produce visually
  equivalent paint), better at higher zooms (ribbon stays
  crisp; the stamp's anti-aliased disk would have softened).
- Test surface: pure-logic `Tessellate*` (10 tests, PR #59) +
  `StrokeEngine` event model (12 tests, PR #60) + the
  unchanged `StrokeEnginePressure` curve coverage (7 tests).
  234/234 pass; the GPU rendering swap inherits the same
  manual smoke verification gate the rest of the GPU code
  uses (no GPU on CI).

## Follow-ups

- Per-stroke vertex-buffer caching once stroke counts grow
  enough that per-frame re-tessellation shows up in profiles.
- Eraser tool (stroke-level + area-level) once the document
  model has a `RemoveStrokeCommand` to pair with.
- Variable-colour strokes (rainbow / colour modulation) —
  `RibbonVertex` already carries per-vertex colour for this.
- `.noted` v2 schema embedding strokes alongside the block
  tree (the data is already lossless, only the writer/reader
  needs extension).
