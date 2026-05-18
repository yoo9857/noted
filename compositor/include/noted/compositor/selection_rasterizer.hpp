#pragma once

// Selection rasterizer — materializes a `domain::Selection` into a
// `gpu::SelectionMask`.
//
// V1 strategy: CPU rasterizes the rect list into a host-visible
// staging buffer, a single `vkCmdCopyBufferToImage` uploads the
// result. The trade-off is the typical "marquee selection" use case —
// 1..N rectangles, infrequent updates (only on user gesture). At that
// scale the CPU pass is fast and the upload is one tightly-packed
// copy; spending an entire shader pipeline + dynamic-rendering pass
// would be overkill.
//
// A follow-up can grow this into a shader rasterizer once selections
// gain polygon / lasso geometry (every polygon edge becomes a draw
// call). The class boundary stays: callers see the same `record()`
// API and the GPU-side detail changes underneath.
//
// Public API split:
//   - `rasterize_to_buffer` — pure function, no GPU. Unit-testable.
//     Writes a packed R8 image into the caller-supplied span.
//   - `SelectionRasterizer` — owns the staging buffer, records the
//     transition / copy / transition sequence.
//
// Rationale: see docs/architecture/0021-selection-mask-gpu.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <vulkan/vulkan.h>

#include "noted/domain/selection/selection.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/buffer.hpp"

namespace noted::gpu {
class Allocator;
class SelectionMask;
}  // namespace noted::gpu

namespace noted::compositor {

// Pure function: rasterize a Selection into a packed R8 buffer.
//
// Layout: `dest.size()` must equal `extent.width * extent.height`.
// Rows are tightly packed (row stride == width). Each rect in the
// selection is clipped to the mask bounds, then every pixel inside
// the (intersected) rect is set to `fill_value` (255 by default for
// a hard selection). Pixels outside every rect retain their prior
// value — callers seeding with 0 produce a binary mask; seeding with
// 255 produces a punch-through.
//
// No allocations. noexcept. The function does not zero `dest` for
// you; pass an already-cleared span if you need 0 outside the
// selection. `clear_destination` (overload below) wraps that pattern.
void rasterize_to_buffer(const noted::domain::Selection& selection,
                         VkExtent2D extent,
                         std::span<std::uint8_t> dest,
                         std::uint8_t fill_value = 255) noexcept;

// Convenience: memset the destination to 0 and then rasterize. Most
// callers want this — produces a clean binary mask from `selection`.
void clear_and_rasterize(const noted::domain::Selection& selection,
                         VkExtent2D extent,
                         std::span<std::uint8_t> dest,
                         std::uint8_t fill_value = 255) noexcept;

// GPU recorder: owns a persistently-mapped staging buffer sized to
// the mask, writes the rasterized pixels into it, and records the
// barrier / copy / barrier sequence onto the supplied command buffer.
//
// The instance is sized for a specific (max) mask extent at create
// time. `resize_staging()` rebuilds the buffer when the mask grows.
// Must not be called while a prior submission is still reading the
// staging buffer — for v1 selections rebuild only on user gesture, so
// the caller wait_idles before rebuilding.
class SelectionRasterizer {
public:
    [[nodiscard]] static auto create(const noted::gpu::Allocator& allocator,
                                     VkExtent2D extent) -> Result<SelectionRasterizer>;

    SelectionRasterizer(SelectionRasterizer&&) noexcept = default;
    auto operator=(SelectionRasterizer&&) noexcept -> SelectionRasterizer& = default;
    SelectionRasterizer(const SelectionRasterizer&) = delete;
    auto operator=(const SelectionRasterizer&) -> SelectionRasterizer& = delete;
    ~SelectionRasterizer() = default;

    // Rebuild the staging buffer for a different mask extent. No-op
    // when the new size matches the cached capacity.
    [[nodiscard]] auto resize_staging(const noted::gpu::Allocator& allocator,
                                      VkExtent2D extent) -> Result<void>;

    // Record:
    //   1. Transition `mask` to TRANSFER_DST_OPTIMAL.
    //   2. Memcpy the rasterized rects into the staging buffer.
    //   3. vkCmdCopyBufferToImage2 from staging → mask.
    //   4. Transition `mask` to SHADER_READ_ONLY_OPTIMAL.
    //
    // The mask's current_layout() must match the cached layout the
    // mask itself is tracking — both updates flow through
    // `SelectionMask::transition_to()`, so the cache stays in sync.
    //
    // Errors only on staging-buffer write failure (e.g. mask grew
    // since the last `resize_staging()`).
    [[nodiscard]] auto record(VkCommandBuffer cb,
                              const noted::domain::Selection& selection,
                              noted::gpu::SelectionMask& mask) -> Result<void>;

    [[nodiscard]] auto capacity() const noexcept -> VkExtent2D { return capacity_; }

private:
    SelectionRasterizer(noted::gpu::Buffer staging, VkExtent2D capacity) noexcept;

    std::optional<noted::gpu::Buffer> staging_;
    VkExtent2D capacity_{0, 0};
};

}  // namespace noted::compositor
