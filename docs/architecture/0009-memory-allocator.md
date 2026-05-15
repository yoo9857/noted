# ADR 0009: Memory allocator + upload model

**Status:** Accepted
**Date:** 2026-05-15

## Context

A real image editor allocates hundreds of GPU resources — every tile is
an Image, every history step may hold a Buffer, every shader has uniform
and storage buffers. Vulkan's raw `vkAllocateMemory` is impractical: it
caps allocations per device, requires manual sub-allocation, and forces
us to write memory-type selection logic. We need an allocator that
matches what AAA engines actually use.

We also need an upload model. Vulkan has no "memcpy this CPU buffer to
this GPU image" call — every upload is a staging buffer + command
recording + queue submission. The choice is whether to expose that
machinery to every caller or to wrap a sensible default.

## Decision

### Allocator: Vulkan Memory Allocator (VMA) v3.1.0

VMA is fetched at configure time via `FetchContent`. The CMake target
`VulkanMemoryAllocator` is linked publicly into the engine module; the
implementation TU (`VMA_IMPLEMENTATION`) lives in
`engine/src/gpu/allocator.cpp` and nowhere else.

`gpu::Allocator` wraps `VmaAllocator` with the engine's move-only RAII
contract. One Allocator per `gpu::Device`.

### Resource wrappers

`gpu::Buffer` wraps `VkBuffer + VmaAllocation`. Construction takes a
`BufferCreateInfo` that chooses one of three `MemoryUsage` modes:

- `gpu_only`   — device-local, fast path
- `cpu_to_gpu` — HOST_VISIBLE, staging / per-frame uniforms
- `gpu_to_cpu` — HOST_VISIBLE, readbacks

`persistent_map = true` keeps the allocation mapped for the buffer's
lifetime so subsequent writes hit a cached pointer.

`gpu::Image` wraps `VkImage + VkImageView + VmaAllocation`. The view is
built immediately with the requested `view_type` and `aspect`. Images
are always device-local; HOST_VISIBLE images are intentionally not on
the menu.

### Upload model

`upload_image_pixels(allocator, device, dst, pixels, info)` performs a
synchronous staging upload:

1. Allocate a `cpu_to_gpu` Buffer the size of `pixels`.
2. memcpy pixels into the (persistently-mapped) staging buffer.
3. Allocate a transient `CommandPool` + `CommandBuffer`.
4. Record: `current_layout` → `TRANSFER_DST_OPTIMAL` (sync2 barrier)
   → `vkCmdCopyBufferToImage2` → `TRANSFER_DST_OPTIMAL` → `final_layout`.
5. Submit on the caller's queue, wait on a one-shot fence.

This is the simplest sufficient path. It is slow for streaming many
tiles per frame — that's why `feat/transfer-scheduler` will introduce
a `TransferScheduler` later: one persistent transfer queue, one
ring-buffered staging pool, one timeline semaphore per submission, no
per-call fence creation.

## Alternatives considered

- **Hand-rolled allocator.** A 1k-line job at minimum that recreates
  what VMA already gives us. Done once, expensive forever.
- **vk-bootstrap / volk + manual VMA.** Volk's loader generation is
  appealing but we already pay one indirection for `find_package(Vulkan)`;
  not worth the complexity.
- **`shared_ptr<VmaAllocation>`.** Hides ownership; we want it visible.
- **Async-only upload from day one.** The single async path would carry
  more state than the typical "load 5 textures at startup" caller needs.
  Defer.

## Consequences

- Tile-based image storage (`engine/tile/`) implements its backing as
  a pool of `gpu::Image` objects allocated via VMA — no special-case code.
- Plugins that allocate GPU resources do it through the same API; the
  WASM sandbox bookkeeps allocations the same way for everything.
- Memory dumps via `vmaCalculateStats` give us a real picture for the
  debug overlay we'll add in `feat/debug-overlay`.
- One indirection layer over Vulkan, well-understood by every Vulkan
  engineer we'd ever hire.
