#pragma once

// One-shot GPU work — synchronous "record one thing, submit, wait."
//
// The engine has two valid contexts for recording commands:
//
//   1. The frame loop — the renderer owns N command pools and buffers,
//      rotates frames-in-flight, never blocks. Everything per-frame goes
//      here. See `noted/engine/gpu/renderer.hpp`.
//
//   2. One-time admin work that must happen outside any render pass and
//      must complete before the caller moves on — initial layout
//      transitions, clear-to-default, single-use uploads. That's this
//      helper. It allocates a transient command pool + primary buffer,
//      records the caller's work, submits with a fence, blocks until the
//      fence signals, then tears the pool down.
//
// **Do not call from inside the frame loop.** The whole point is to
// drain the queue and wait — that defeats frames-in-flight. The intended
// call sites are construction-time setup (e.g.
// `LayerCompositor::create()` initializing its dummy mask) and rare
// admin paths (e.g. resize-time resource re-initialization).
//
// The caller's `record` lambda receives the active VkCommandBuffer.
// It runs OUTSIDE any render pass — `vkCmdBeginRendering` /
// `vkCmdEndRendering` are forbidden because they'd capture state the
// helper can't clean up on error.
//
// Errors:
//   - invalid_argument: queue or queue_family unset, or record fn is empty.
//   - gpu_validation_failed: pool/buffer alloc, fence create, submit failure.
// The wait itself ignores its result (matches upload_image_pixels) so a
// device-lost on the wait does not mask the real cause — that surfaces
// on the next renderer call.

#include <cstdint>
#include <functional>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

struct ImmediateSubmitInfo {
    // Caller picks the queue. The graphics queue is the safe default —
    // the helper does not require any queue capability beyond what the
    // recorded commands need.
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = UINT32_MAX;
};

// `std::function` is acceptable here: the helper runs once per call site
// over the lifetime of a resource, so the heap allocation behind the
// erased callable is amortized away. Header stays slim — consumers do not
// need to template-instantiate the helper.
using ImmediateRecordFn = std::function<void(VkCommandBuffer)>;

[[nodiscard]] auto immediate_submit(const Device& device,
                                    const ImmediateSubmitInfo& info,
                                    const ImmediateRecordFn& record) -> Result<void>;

}  // namespace noted::gpu
