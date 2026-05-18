# ADR 0022: Compositor masking — selection mask gates layer output

**Status:** Accepted
**Date:** 2026-05-18

## Context

ADR 0020 / 0021 established the selection pipeline:
1. `domain::Selection` — CPU rect-list with set algebra (ADR 0020).
2. `gpu::SelectionMask` — R8 image (ADR 0021).
3. `compositor::SelectionRasterizer` — fills the mask from the
   selection.

The last missing piece is **consumption**: the `LayerCompositor`
(ADR 0019) needs to honor the mask. Inside the selected region a
layer composites normally; outside, the layer's contribution to the
pixel becomes zero.

The same mask will eventually gate `StrokeEngine` deposition and
adjustment-layer effects, but the highest-value first consumer is
the layer compositor — it's the only pipeline that touches every
pixel of every frame.

## Decision

Three coordinated changes:

1. **Shader (`shaders/layer.slang`)** — vertex shader now forwards
   the canvas-space UV; fragment samples a `Sampler2D u_mask` at
   `(set=0, binding=0)` and multiplies the push-constant color by
   `mask.r`.
2. **`LayerCompositor`** — owns:
   - A descriptor set layout, pool, single descriptor set, and a
     linear-clamp sampler for the mask binding.
   - A 1×1 R8 "all selected" **dummy mask** initialized lazily on
     the first `composite()` call.
   - An optional `const gpu::SelectionMask*` parameter on
     `composite()`. When null, the dummy is bound.
3. **`composite()` signature** —
   `composite(cb, extent, graph, store, mask = nullptr)`. The
   trailing default keeps every existing call site valid.

### Why a dummy mask instead of a shader branch

Three options exist for the "no mask supplied" case:

| Option | Shader cost | API cost | Notes |
|---|---|---|---|
| **Dummy 1×1 mask (chosen)** | 1 bilinear tap → 1.0 | None — caller passes nullptr | Mask read always happens; predictable cost; shader stays unconditional. |
| Branch on push-constant flag | `if(use_mask) ... else ...` | Push-constant grows by 4 bytes | Hot path branch is essentially free on modern GPUs, but the shader bifurcates and validation gets noisier. |
| Two pipeline variants | None | `LayerCompositor` doubles its pipeline count (8 instead of 4) | Pipeline state explosion; the descriptor-set layout would have to vary too, breaking the "one bind per composite" property. |

The 1×1 dummy is the right answer at this scale. Sampling a 1×1
texture in a fragment shader is one bilinear tap on a texel that
sits entirely in cache — the cost is dominated by the descriptor
fetch which we'd pay either way. The dummy is initialized once
(`ensure_dummy_initialized_`) on the first composite call via a
clear-color-image; subsequent calls just rebind it.

If profiling shows the constant-1.0 multiply matters (it won't,
on any modern GPU), we revisit. Until then, the **shader stays
unconditional** and the API stays clean.

### Why lazy dummy initialization

Two options for clearing the 1×1 dummy to white (= "all selected"):

- **Eager: clear during `create()`.** Requires `create()` to take a
  queue + queue_family, allocate a one-shot command buffer, submit,
  wait on a fence. Adds machinery that exists nowhere else in the
  compositor, and `create()` is currently failure-mode-free for
  callers that have a working device + allocator.
- **Lazy: first composite() call.** Adds `bool dummy_initialized_`,
  records the clear onto the caller's command buffer at the top of
  the first `composite()`. Zero extra API surface, zero submit calls,
  the clear's one barrier-clear-barrier is recorded in the same
  pipeline-batching window as the rest of the frame.

Lazy wins by a wide margin. The trade-off — the first frame is
1.001× slower than steady state — is not measurable.

### Descriptor strategy: one set, rewritten per call

`composite()` binds **one** descriptor set; the same set is
rewritten on every call via `DescriptorWriter::commit()` (which
expands to `vkUpdateDescriptorSets`). Per Vulkan rules:

- A descriptor set may be updated freely between submissions.
- A descriptor set must NOT be updated while a command buffer that
  uses it is still pending on the GPU.

The renderer waits on the prior frame's fence before re-recording
the current frame's commands (ADR 0008), so by the time
`composite()` runs the set is no longer pending. Safe.

Alternatives considered:
- **One set per frame in flight.** Cleanest but redundant — the
  rebind cost is identical and the per-frame storage is wasted at
  this scale (one descriptor set per N frames in flight, all
  pointing at the same mask 99% of the time).
- **Update-after-bind.** Requires extension features and adds pool
  configuration. Premature.

Revisit when the compositor binds multiple texture-resident
resources per frame (e.g. adjustment LUTs, brush atlases) and a
fully-bindless setup becomes worth the descriptor-buffer ADR.

### Member declaration order (RAII)

Vulkan's destruction-order rules dictate the field layout:
- VkPipeline must outlive VkPipelineLayout? Actually the inverse —
  pipelines depend on the layout and must be destroyed first.
- VkDescriptorSets (owned by the pool) must die before their
  VkDescriptorSetLayout.

C++ destroys members in reverse declaration order, so the LayerCompositor
class declares `set_layout_` first (dies last), then `pipeline_layout_`,
then `descriptor_pool_`, then leaf resources, then `pipelines_` (dies
first). A short comment in the header explains the rule for the
next reader.

## Alternatives considered

- **Inline mask into the canvas alpha at rasterize time.** Conflates
  selection (a UI concept) with canvas state (a render state). Breaks
  the moment two tools want different selection scopes simultaneously
  (e.g. mask brush vs. fill bucket). Rejected.
- **Stencil-buffer mask.** Stencils are 8-bit and per-pixel binary
  — fine for v1 marquee, but feathered selections (next year) need
  float mask. Doing it all in R8 from the start avoids a migration.
- **Per-layer mask binding.** Each layer would carry its own selection.
  The Photoshop model uses one selection per document; adopt that here.
- **Pass `SelectionMask&` instead of `const SelectionMask*`.** A
  reference forces the caller to always have a real mask, which
  hides the "no selection = all selected" semantic and forces every
  call site to construct or own a default mask. Optional pointer
  with a sensible default makes the API friendly to "first 80% of
  frames where nothing is selected."

## Consequences

- `LayerCompositor::CreateInfo` gains an `Allocator*` field for the
  dummy mask. No existing call sites break — main.cpp doesn't wire
  the compositor yet (it's queued behind UI work).
- `shaders/layer.slang` grows a fragment binding and a UV varying.
  Recompiles to SPIR-V via the existing slangc pipeline.
- All 106 unit tests pass unchanged — the pure-logic helpers
  (`blend_state_for`, `resolve_composition`, `LayerPayloadStore`) are
  untouched. New behavior is GPU-only and belongs in a future
  integration test (`tests/integration/`).
- No new exception paths. Every failure flows through `Result<T>`.

## Follow-ups

- **`tests/integration/layer_compositor_gpu_test.cpp`** — once the
  CI matrix gains a software-Vulkan smoke test (lavapipe), assert
  that a 4-rect selection masks a red full-screen layer to exactly
  the selected pixels.
- **Stroke gating** — `StrokeEngine` reuses the same descriptor
  pattern (sample mask, multiply alpha) so dragging outside the
  selection deposits nothing.
- **Adjustment layers** — color-correction / curves shaders adopt
  the same set=0/binding=0 mask convention. A shared mini-header
  in `shaders/` can document the convention.
- **Feathered selections** — when polygon / lasso lands and the
  rasterizer writes intermediate alpha, no shader change needed: the
  multiply already honors fractional mask values.
