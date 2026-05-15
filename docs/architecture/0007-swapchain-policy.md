# ADR 0007: Swapchain policy — format, present mode, resize

**Status:** Accepted
**Date:** 2026-05-15

## Context

A swapchain is a tiny but unforgiving piece of Vulkan: format selection
affects color correctness, present mode trades latency against tearing,
and a wrong resize policy locks up the window for seconds. We want one
canonical policy so every renderer in the project behaves the same.

## Decision

### Format

Prefer `VK_FORMAT_B8G8R8A8_SRGB` with `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`.
This gives us:

- automatic linear-to-SRGB conversion on write, which matches our internal
  linear scene-referred working space;
- color values displayed by the OS in the spec-mandated way for an SRGB
  monitor.

Fall back to `B8G8R8A8_UNORM` (gamma applied by the shader), then to
whatever the driver lists first. We don't currently support HDR swapchains;
that's a separate ADR when we ship a 10-bit pipeline.

### Present mode

Priority order: `VK_PRESENT_MODE_MAILBOX_KHR` → `VK_PRESENT_MODE_FIFO_RELAXED_KHR`
→ `VK_PRESENT_MODE_FIFO_KHR`.

- **MAILBOX** — triple buffered, dropped frames OK. Lowest input latency,
  no tearing. Default for desktop. Required on the discrete-GPU path.
- **FIFO_RELAXED** — vsync but allows tearing if a frame missed deadline.
  Reasonable middle ground when MAILBOX is unavailable (some mobile drivers).
- **FIFO** — hard vsync, guaranteed by the spec. Used as final fallback
  and forced when the `gpu.force_vsync_fifo` harness flag is on (helpful
  for power-budget testing).

### Image count

Request 3 by default (triple buffered), clamp to surface caps. Do not
hard-code; some drivers (Intel iGPU on Linux) return `maxImageCount = 2`.

### Resize

When `vkAcquireNextImageKHR` or `vkQueuePresentKHR` returns
`VK_ERROR_OUT_OF_DATE_KHR` or `VK_SUBOPTIMAL_KHR`, we:

1. Block on `vkDeviceWaitIdle` — cheap once per resize, simpler than
   tracking in-flight work per swapchain.
2. Call `Swapchain::recreate(extent)`, which passes the previous handle as
   `oldSwapchain` so the driver can re-use VRAM.
3. Re-issue the frame from the beginning.

Window resize during dragging fires `OUT_OF_DATE` many times per second.
The recreate path must therefore stay allocation-thrifty: we keep
`graphics_family_` and `present_family_` on the Swapchain object so resize
doesn't re-touch the Device.

## Alternatives considered

- **Default to FIFO everywhere.** Predictable but laggy. Photoshop and
  competitors run MAILBOX-equivalent paths; we should too.
- **Recreate on every resize event instead of OUT_OF_DATE.** Same end
  result but couples us to the platform event pump.
- **Per-image fences instead of `vkDeviceWaitIdle` on resize.** Lower
  pause but adds complexity; defer until we have evidence that the wait
  is user-visible.

## Consequences

- Color-correct rendering "just works" because SRGB conversion is in the
  swapchain, not the shader.
- The harness flag `gpu.force_vsync_fifo` lets QA and CI reproduce vsync
  behavior without code changes.
- Resize cost is one device idle plus one swapchain rebuild per
  out-of-date event. Acceptable for a desktop creative tool; revisit if
  we ship to mobile.
