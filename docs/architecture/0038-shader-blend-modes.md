# ADR 0038 — Shader-based layer blend modes via dynamicRenderingLocalRead

**Status:** Stages 1–2a accepted. Stage 2b (pipeline + snapshot
mechanics) lands in a follow-up.
**Date:** 2026-05-24
**Builds on:** [ADR 0019 (layer compositor)](0019-layer-compositor.md),
[ADR 0028 (compositor frame-safe init)](0028-compositor-frame-safe-init.md).

## Context

`LayerGraph::BlendMode` declares the standard 16-mode Photoshop
set. `LayerCompositor` currently implements 4 of them via
fixed-function Vulkan blend:

- `normal` — `srcF=ONE, dstF=ONE_MINUS_SRC_ALPHA, op=ADD`
- `screen` — `srcF=ONE, dstF=ONE_MINUS_SRC_COLOR, op=ADD`
- `linear_dodge` — `srcF=ONE, dstF=ONE, op=ADD`
- `multiply` — `srcF=DST_COLOR, dstF=ZERO, op=ADD` (opaque-layer
  approximation; semi-transparent multiply needs a shader)

The other 12 (`overlay`, `soft_light`, `hard_light`, `color_dodge`,
`color_burn`, `linear_burn`, `difference`, `exclusion`, `hue`,
`saturation`, `color`, `luminosity`) fall back to `normal` and
increment a `fallback_count_` counter.

None of those 12 are expressible via Vulkan fixed-function blend.
They need the fragment shader to read the **current** color
attachment value, compute the per-mode formula against the
incoming `src`, and output the blended result.

## Decision

### Stage 1 (this PR) — Vulkan 1.4 + `dynamicRenderingLocalRead`

Bump the instance API version `VK_API_VERSION_1_3` → `_1_4` and
add an opportunistic `enable_dynamic_rendering_local_read` flag on
`DeviceCreateInfo` (default ON). `Device::create` queries the
physical device's `VkPhysicalDeviceVulkan14Features.dynamicRenderingLocalRead`
and only chains the feature struct into `pNext` when supported,
so older drivers / devices keep creating the device unchanged.

`Device::has_dynamic_rendering_local_read()` accessor exposes
the resulting state so consumers (initially: the compositor's
follow-up shader-blend pipeline) can branch on it.

Behavioural change in this PR: **none.** Every blend mode keeps
its current routing. The 12 fall-through modes still increment
`fallback_count_`. This is intentionally just the **feature
unlock**.

### Stage 2a (this PR follow-up) — shader written + push struct grown

The fragment-side math for all 12 modes lands now so the next PR
only has to add pipeline + descriptor + snapshot-copy plumbing,
not the formulas themselves:

- `shaders/layer.slang` gains a `ps_layer_shader_blend` fragment
  entry alongside the existing `ps_layer`. The W3C compositing 1.0
  reference math covers all 12 modes via a `switch` on a
  push-constant `mode` ordinal. NORMAL collapses to "over" so
  pushing `mode = 0` round-trips losslessly.
- `LayerPush` (in shader + C++) grows from 16 to 32 bytes —
  `vec4 color + uint mode + 12 bytes pad`. The mode field is
  written every layer regardless of pipeline; FF pipelines
  ignore it, the shader-blend pipeline reads it.
- `shaders/CMakeLists.txt` adds the new entry as a third SPIR-V
  output (`layer.ps_layer_shader_blend.spv`).
- Behavioural change: **none yet**. The 12 modes still route
  through `slot_for(...)` returning the NORMAL slot + a
  `fallback_count_++`; the shader-blend pipeline that *uses*
  the new entry ships in stage 2b. This split keeps the
  shader-math PR independently reviewable and verifiable
  (it's all just Slang code one can step through).

### Stage 2b (follow-up PR) — pipeline + snapshot mechanics

A second graphics pipeline in `LayerCompositor`:

- `blendEnable = VK_FALSE` — the shader's output IS the blended
  pixel, no fixed-function blending.
- Created with `VkPipelineRenderingCreateInfo` chained with
  `VkRenderingInputAttachmentIndexInfoKHR` pointing color
  attachment 0 at input attachment index 0.
- Fragment shader (extension of `shaders/layer.slang`):
  - Reads `dst` via a `SubpassInput<float4>` declared with the
    matching input-attachment binding.
  - Reads `src.rgb` from the existing push constant + multiplies
    by the selection mask.
  - Switches on a new push-constant ordinal `mode` and computes
    the per-mode formula (Photoshop reference math).
  - Outputs the blended `(src OP dst)` straight to the color
    attachment.
- At composite-record time:
  - `vkCmdSetRenderingInputAttachmentIndicesKHR` connects the
    color attachment as input attachment 0 for the current
    rendering scope (cheap — pipeline-state, not a descriptor
    update).
  - The compositor's descriptor set gains one
    `VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT` binding whose view
    points at the canvas color attachment.
  - For each fall-through mode the compositor binds the
    shader-blend pipeline + pushes the mode ordinal, instead
    of binding the NORMAL pipeline + incrementing
    `fallback_count_`.

When the device doesn't support `dynamicRenderingLocalRead`, the
follow-up PR keeps the current `fallback_count_` path — no
behaviour change for those users (matches stage 1's contract).

### Rejected alternatives

- **`VK_EXT_rasterization_order_attachment_access`** — gives
  fragment-order guarantees but does NOT directly expose the
  current attachment value to the shader. Useful for
  programmable-blending under certain platforms but not the
  right primitive here.
- **Ping-pong scratch image + sampler** — would work without
  any Vulkan 1.4 feature: copy canvas → scratch, sample scratch
  in shader, write back to canvas. But it pays a full-canvas
  copy per shader-blend layer and forces the compositor to
  break the caller's `vkCmdBeginRendering` scope (end + barrier
  + begin). 1.4 local-read is the clean modern path; the
  ping-pong fallback can land later if we discover a user with
  a driver that lacks 1.4 features and wants the modes anyway.
- **Subpass input attachments via legacy renderpasses** — the
  rest of the engine uses dynamic rendering. Reintroducing
  renderpass objects for one feature is wrong-layer.
- **Compute pass post-composite** — moves blend out of the
  graphics pipeline entirely. Possible but a much bigger
  architecture shift; not justified for v0.x.

### Out of scope (deliberate)

- **HSL blend modes** (`hue`, `saturation`, `color`,
  `luminosity`) require RGB↔HSL conversion in the shader.
  In scope for stage 2 — the math is well-established and
  cheap; just longer code in the shader switch.
- **Per-mode performance tuning.** All 12 modes route through
  one pipeline + one push-constant ordinal in stage 2. If a
  specific mode becomes a hotspot a specialised pipeline can
  be carved out later — no API change required.
- **Mode-specific opacity behaviour.** Photoshop's "fill
  opacity" vs "layer opacity" distinction (which behaves
  differently under some blend modes) is not in v0.x scope.

## Implementation surface (this PR)

- `app/src/app.cpp` — `api_version = VK_API_VERSION_1_4`.
- `engine/include/noted/engine/gpu/device.hpp` —
  `DeviceCreateInfo::enable_dynamic_rendering_local_read` flag
  (default `true`) + `Device::has_dynamic_rendering_local_read()`
  accessor.
- `engine/src/gpu/device.cpp` — query
  `VkPhysicalDeviceVulkan14Features.dynamicRenderingLocalRead`
  on the physical device, chain the 1.4 features struct only
  when supported.

## Smoke test

Verified locally on GTX 1050 Ti + NVIDIA 1.4-capable driver:
the app launches, runs frames, exits cleanly with zero Vulkan
validation errors. `has_dynamic_rendering_local_read()`
returns true on that hardware. On a device without 1.4
support the feature negotiation downgrades silently — same
visible behaviour as before this PR.
