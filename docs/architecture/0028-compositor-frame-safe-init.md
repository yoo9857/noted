# 0028 — Compositor frame-safe init + descriptor rotation

**Status:** Accepted · **Date:** 2026-05-19 · **Supersedes:** parts of
[0022 — Compositor masking](0022-compositor-masking.md) (the lazy
dummy-mask init paragraph).

## Context

[ADR 0022](0022-compositor-masking.md) gave the compositor a 1×1 "all
selected" dummy mask so the layer fragment shader can stay
unconditional. The dummy's clear-to-white was deferred to the first
`composite()` call to keep `create()` free of command-buffer / queue
plumbing. The implementation also re-wrote a single shared mask
descriptor every frame so the binding always pointed at the
caller-supplied mask (or the dummy when no mask was passed).

Both choices broke Vulkan validation as soon as the compositor was
wired into the canvas pass (ADR 0019 follow-up, PR #40 — first end-to-
end run):

1. `vkCmdClearColorImage` and `vkCmdPipelineBarrier2` (the dummy's
   layout transitions) executed inside the caller's
   `vkCmdBeginRendering` instance. The spec forbids both
   ([VUID-vkCmdClearColorImage-renderpass](https://docs.vulkan.org/spec/latest/chapters/clears.html#VUID-vkCmdClearColorImage-renderpass),
   [VUID-vkCmdPipelineBarrier2-None-09553](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#VUID-vkCmdPipelineBarrier2-None-09553))
   unless `VK_KHR_dynamic_rendering_local_read` is enabled.
2. `vkUpdateDescriptorSets` re-wrote the shared mask set every frame
   while frame N-1's command buffer was still pending. The spec forbids
   that without `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` on the
   binding
   ([VUID-vkUpdateDescriptorSets-None-03047](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html#VUID-vkUpdateDescriptorSets-None-03047)).

Both errors were dormant in the unit-test suite (no GPU on CI) but
flooded stderr the moment a developer ran the app locally. They were
real spec violations — driver tolerance is undefined behavior, not
correctness.

## Decision

**Move all one-time GPU work out of the frame loop, and rotate
descriptor sets per frame-in-flight.**

### One-time init via `gpu::immediate_submit`

A new engine helper, `noted::gpu::immediate_submit(device, info,
record)`, encapsulates the canonical "transient pool + one-time-submit
CB + fence + wait" pattern. The compositor's `create()` calls it
synchronously to perform the dummy mask's clear + transition. By the
time `create()` returns, the dummy is in `SHADER_READ_ONLY_OPTIMAL`
and there is **no per-frame setup state** in the compositor — no
`dummy_initialized_` flag, no first-call branch, no barriers in
`composite()`.

The contract on `composite()` strengthens accordingly: it records draws
and at most one descriptor write per call, never barriers and never
clears. It is now legal to call between `vkCmdBeginRendering` and
`vkCmdEndRendering` without spec violations.

### Per-frame-in-flight descriptor sets

The compositor allocates `frames_in_flight` descriptor sets (configurable
via `CreateInfo::frames_in_flight`, must equal the renderer's value)
and tracks an internal `frame_counter_`. Each `composite()` call picks
`frame_slots_[frame_counter_ % N]`, writes only if the cached
`VkImageView` for that slot differs from the new bind target, and
binds it. The renderer's existing fence wait on slot N guarantees the
GPU has finished consuming slot N before we hand back the same
command buffer, so writing slot N is always safe — no
`UPDATE_AFTER_BIND_BIT` needed.

In the common case (same dummy mask every frame), after the first N
frames the descriptor write is skipped entirely. The hot path
collapses to one `vkCmdBindDescriptorSets` plus the per-layer draws.

## Alternatives

1. **`VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` on a single shared
   set.** Vulkan-correct, smaller pool footprint. Rejected because the
   set still cannot be touched while it's bound to a CB recording draws
   that read it — the spec narrows the unsafe window but does not
   close it. The per-frame-set pattern is the same advice every Vulkan
   tutorial gives, and it scales to any binding flag policy.
2. **`VK_KHR_dynamic_rendering_local_read` + barriers inside the
   render pass.** Would silence the warning without restructuring, but
   the underlying call pattern is still wrong (you cannot
   `vkCmdClearColorImage` inside a render pass under that extension
   either — only `vkCmdPipelineBarrier2` becomes legal). Adding an
   extension to dodge a design flaw is the wrong layer of fix.
3. **Push descriptors (`VK_KHR_push_descriptor`).** Removes the
   allocated set entirely. Attractive, but adds an extension dep and
   changes the compositor's resource ownership story. Worth revisiting
   when `VK_EXT_descriptor_buffer` (ADR 0011's P5 #14) lands — at that
   point the binding model is rebuilt wholesale.
4. **Move dummy init into a `prepare()` method the caller invokes
   before `vkCmdBeginRendering`.** Workable, but threads a new
   call-order contract through `main.cpp` and the
   `Renderer::DrawCallback` signature. Synchronous init at create()
   keeps the compositor self-contained.

## Consequences

- `LayerCompositor::CreateInfo` requires `graphics_queue`,
  `graphics_family`, and `frames_in_flight`. The first two are needed
  only for the one-time init; if the GPU side ever ships a separate
  setup queue, this is the place to thread it through.
- `ensure_dummy_initialized_` and `dummy_initialized_` are gone — the
  hot path has fewer branches, frame 0's CPU cost dropped from 83 ms
  to 7.6 ms on a GTX 1050 Ti smoke (init no longer happens lazily on
  the first frame).
- Descriptor pool grows from 1 set to N (N = 2 today). The
  `combined-image-sampler` count in the pool sizes scales with N.
- The `frames_in_flight` value must match the renderer's. `main.cpp`
  pins both with a single `constexpr kFramesInFlight = 2` constant.
- `gpu::immediate_submit` is now available to every engine consumer.
  `upload_image_pixels` (which has its own copy of the same pattern)
  is a candidate for a follow-up refactor, deferred so this PR stays
  focused.
- Stderr is clean — the runtime claim "**zero Vulkan validation
  errors**" in HANDOFF.md is once again accurate.

## Future caller note (SelectionRasterizer)

`SelectionRasterizer::record()` performs the same kind of transition →
copy → transition sequence on a caller-supplied command buffer. Its
docstring already implies "outside any render pass," and today no
production code path calls it — but when P3's mask-driven editing
lands, the caller must invoke `record()` BEFORE
`vkCmdBeginRendering(canvas)`, not from inside the canvas draw
callback. The same lesson applies; same fix pattern (extend
`render_with_canvas` with a pre-canvas hook, or schedule mask updates
in a separate submit) waits for that PR.
