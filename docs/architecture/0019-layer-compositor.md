# ADR 0019: Layer compositor — GPU render of the layer graph

**Status:** Accepted
**Date:** 2026-05-16

## Context

`domain::LayerGraph` (ADR 0016) holds the topology and per-node
parameters of the Photoshop side. It has zero GPU dependencies by
design. To put pixels on screen we need a **compositor** that walks
the graph in topological order and produces the document's image in
the canvas render target (ADR 0014).

Three concerns to settle:

1. **Where does the compositor live?** `engine/` is the GPU layer
   but cannot depend on `domain/` without a cycle. `domain/` is
   pure-logic by charter; adding GPU code there breaks that charter.
   Neither is right.
2. **How are per-layer payloads represented?** `LayerGraph` carries
   no pixel data. The compositor needs the actual contribution per
   node — color, image, stroke list, etc.
3. **Which blend modes does the MVP ship?** All 16 Photoshop modes
   eventually, but most cannot be expressed via Vulkan's
   fixed-function blend state and need a shader-based composite. The
   MVP must pick a believable subset and document the fallback.

## Decision

### New `compositor/` module

A new module sits above both `engine/` and `domain/`, depending on
both. Module layering:

```
app
 └── compositor   (NEW: links engine + domain)
      ├── engine
      └── domain  (already depends on engine)
```

This restores acyclicity, keeps `domain/` GPU-free, and reserves
`engine/` for foundation-layer code. All compositor code lives in
the `noted::compositor` namespace under
`compositor/include/noted/compositor/` and `compositor/src/`.

### `LayerPayloadStore` — bridge between graph and pixels

```cpp
using LayerPayload = std::variant<SolidColor /*, Bitmap, Stroke, ...*/>;

class LayerPayloadStore {
    void set(LayerId, LayerPayload);
    const LayerPayload* find(LayerId) const noexcept;  // nullptr = skip
    void erase(LayerId);
    void clear();
};
```

The store is a `LayerId → payload` map maintained by the host (app /
brush UI / future bitmap importer). Domain operations on
`LayerGraph` (insert, move, rename, set_blend, …) don't touch it;
payload mutations don't touch the graph. They join at composite-time.

MVP ships one payload variant: `SolidColor { rgba }`. Every other
future kind — bitmap pixels, stroke buffers, adjustment parameters
— adds a `std::variant` alternative and one switch branch in the
compositor. The plumbing is set.

### Per-mode pipelines, fixed-function blend, single shader

The compositor builds **N pipelines** at creation time (one per
fixed-function-supported blend mode), sharing the same
vertex/fragment shaders (`shaders/layer.slang`). Each pipeline
differs only in `VkPipelineColorBlendAttachmentState`. At composite
time we bind the matching pipeline per layer and dispatch one
fullscreen triangle.

**Why one pipeline per mode**: pipeline state is immutable in
Vulkan; per-frame pipeline rebuilds are forbidden by ADR 0010. The
alternative — `VK_EXT_extended_dynamic_state3` for dynamic blend
state — is part of ADR 0011's modernization queue but not enabled
in MVP because not every GPU supports it. Per-mode pipelines work
on every Vulkan 1.4 device.

### MVP blend-mode coverage

| Mode            | Status        | Implementation                                                                 |
|-----------------|---------------|--------------------------------------------------------------------------------|
| normal          | ✓ FF          | `src = ONE, dst = ONE_MINUS_SRC_ALPHA, ADD`                                    |
| screen          | ✓ FF          | `src = ONE, dst = ONE_MINUS_SRC_COLOR, ADD`                                    |
| linear_dodge    | ✓ FF          | `src = ONE, dst = ONE, ADD` (additive)                                         |
| multiply        | ✓ FF (opaque) | `src = DST_COLOR, dst = ZERO, ADD` — assumes layer.a = 1                       |
| overlay         | fallback      | mathematically not FF-expressible                                              |
| soft_light      | fallback      |                                                                                |
| hard_light      | fallback      |                                                                                |
| color_dodge     | fallback      |                                                                                |
| color_burn      | fallback      |                                                                                |
| linear_burn     | fallback      |                                                                                |
| difference      | fallback      | needs `abs(src - dst)` — no FF abs                                             |
| exclusion       | fallback      |                                                                                |
| hue/saturation/color/luminosity | fallback | colorspace math — has to be a shader                              |

Fallback = bind the NORMAL pipeline instead. The compositor
increments `fallback_count()` per fallback draw so the eventual
shader-based compositor can be benchmarked against this MVP and the
HUD can warn users that their multiply-with-alpha or overlay won't
look right yet.

### Composition resolve as a pure function

```cpp
auto resolve_composition(const LayerGraph&, const LayerPayloadStore&)
    -> Result<std::vector<CompositorEntry>>;
```

Returns the list of `(id, blend, opacity, payload*)` tuples in
topological order, with invisible nodes and payload-less nodes
filtered out. **Pure** — no GPU, no command buffer. The unit tests
exercise it directly: topology, visibility filter, payload filter,
graph-error propagation, opacity / blend round-trip.

The actual `LayerCompositor::composite()` is the thin wrapper that
binds pipelines + push-constants and dispatches draws.

### Pre-multiplied alpha at the boundary

The compositor multiplies `payload.rgb` by `payload.a * node.opacity`
before pushing the constant. This produces a pre-multiplied color
that the fixed-function blend math expects:

```
out = src.rgb + dst.rgb * (1 - src.a)
```

is the "over" operator only when `src.rgb` already has alpha baked
in. The shader is unaware — it just outputs the push constant
verbatim.

## Alternatives considered

- **Put compositor in `domain/`.** Easiest CMake change. Breaks the
  module's "pure logic" charter and clouds the dependency graph for
  every future GPU-touching feature that needs domain types.
  Rejected.
- **Compositor in `engine/`, with domain types accessed via opaque
  IDs only.** Removes the cycle but turns every API into untyped
  uint64_t-passing. The compile-time safety we get from
  `domain::LayerId` evaporates. Rejected.
- **`VK_EXT_extended_dynamic_state3` for dynamic blend.** Cleaner
  long-term (one pipeline, blend state per draw), but limits us to
  GPUs that expose the extension. Revisit alongside the
  shader-based compositor.
- **Shader-based composite of every mode now.** Right answer for
  correctness; substantial extra shader work for an MVP. Lands as
  `feat/layer-compositor-shader` once we need overlay / difference /
  etc. for real.
- **One pipeline, dynamic blend via push constant fake** (e.g., a
  shader that reads blend mode from push constant and does the math
  in fragment, with NORMAL pipeline blend underneath). Effectively
  the shader-based approach with the FF blend turned off; same
  scope cost.

## Consequences

- The Photoshop side now has a working pipeline:
  `LayerGraph + LayerPayloadStore → LayerCompositor → canvas`.
  Adding a feature is "add a `LayerKind` payload variant, add a
  visit branch in `composite()`, optionally add a pipeline slot".
- `compositor/` joins the module list. The build matrix picks it up
  automatically (CMake's `add_subdirectory` recursion).
- 13 new unit tests pass on every host: the blend-factor table
  (4 supported modes + 12-mode fallback batch) and the pure
  `resolve_composition` walk (empty, visibility filter, payload
  filter, topological order, blend/opacity carry-through), plus
  the payload-store basic semantics.
- The compositor counts every fallback. The eventual shader
  composite ships against this counter — when it reaches zero on
  realistic documents, the FF compositor can retire.

## Follow-ups

- **Per-kind payloads** — `Bitmap { gpu::Image* }`, `Stroke { ... }`,
  `Adjustment { params }`. Each is one PR adding a variant case
  and one branch in `composite()`.
- **Shader-based blend modes** (`feat/layer-compositor-shader`) —
  ping-pong canvases + a fragment shader that does all 16 modes.
  Replaces the fallback path; FF pipelines stay as a fast path for
  the 4 modes they handle.
- **Group / mask kinds** — adjust how children/inputs feed into the
  parent's draw. Likely a different draw mode (offscreen scratch
  + multiply mask).
- **`VK_EXT_extended_dynamic_state3` adoption** — collapse the
  N-pipeline array into a single pipeline with dynamic blend state.
- **Compositor → app wire-up** — main.cpp creates a
  `LayerCompositor`, a `LayerPayloadStore`, and a small demo
  `LayerGraph` so the user can see actual compositing in the running
  app. Deferred to a follow-up PR to keep this one focused on
  primitives + tests.
