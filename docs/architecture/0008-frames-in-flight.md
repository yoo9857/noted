# ADR 0008: Frames-in-flight policy

**Status:** Accepted
**Date:** 2026-05-15

## Context

The CPU and the GPU run independently. If the CPU records frame N+1 before
the GPU finishes frame N, the GPU might still hold the command buffer.
If the GPU is held back waiting for the CPU, throughput drops. We need a
small fixed number of frame "slots" and explicit sync between them.

## Decision

**N = 2** frames in flight by default. The `Renderer` rotates through
`frame_counter % N` slots. Each slot owns:

- one `CommandPool` (Vulkan pools are not thread-safe; per-slot avoids
  contention with future render threads);
- one primary `CommandBuffer` allocated from that pool;
- one `FrameSync` containing `image_available`, `render_finished`
  semaphores and an `in_flight` fence.

Per-frame cycle:

```
 wait(in_flight_fence)
 acquire image  ─signals→ image_available
 reset(in_flight_fence)
 reset+record command buffer (transition→clear→transition)
 submit  wait=image_available  signal=render_finished, in_flight
 present wait=render_finished
```

The fence is created in the **signaled** state so the very first wait is
an immediate no-op.

### Swapchain images vs frames-in-flight

These are independent counts. The swapchain may hand back 2, 3, or more
images depending on the surface. We submit work against whichever image
`vkAcquireNextImageKHR` returns, using our 2 frame-in-flight slots to
sync. We do **not** keep a per-image fence pool — relying on the per-slot
fence is sufficient because the CPU never has more than N submits in
flight.

### OUT_OF_DATE / SUBOPTIMAL handling

`render_frame` returns a `Result` with code:

| Code                          | Meaning                                |
|-------------------------------|----------------------------------------|
| `gpu_swapchain_out_of_date`   | Recreate before the next frame.         |
| `gpu_swapchain_suboptimal`    | Recreate at convenience (frame succeeded). |
| `gpu_validation_failed` etc.  | Fatal — caller should bail.            |

The app loop reacts by calling a single `recreate_swapchain()` closure
that:

1. `device->wait_idle()`.
2. Skips if the window is minimized (fb_size 0x0).
3. `swapchain.recreate(...)` with the current window framebuffer size.
4. `renderer.rebind_swapchain(...)` — currently a no-op, hook for the
   day we add per-image render targets.

The next iteration of the main loop re-attempts `render_frame` with the
new swapchain.

## Alternatives considered

- **N = 1.** Eliminates the second pool/buffer but forces the CPU to wait
  on every frame. ~25% throughput loss on the typical workload.
- **N = 3 (or more).** Adds input latency proportional to extra frames.
  MAILBOX present already triple-buffers on the GPU side; doubling on
  the CPU side too is overkill.
- **Per-image fences.** Common in Vulkan tutorials. Adds complexity for
  no measurable benefit at N=2 over a wide image-count range.
- **Timeline semaphores** (Vulkan 1.2). Cleaner mental model, fewer
  primitives. Worth migrating to once the renderer needs sync between
  multiple queues; not yet.

## Consequences

- Pipeline is shallow enough that the input-to-photon latency is
  ~2 frames (one to record, one to display).
- The `recreate_swapchain` path is fast: one `vkDeviceWaitIdle` + one
  swapchain rebuild. Acceptable for desktop resize.
- Code stays uniform — every GPU resource follows the same RAII pattern,
  every error surfaces via `Result<T>`.
