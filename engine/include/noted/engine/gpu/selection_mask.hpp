#pragma once

// Selection mask — single-channel R8 image that gates per-pixel work.
//
// The compositor and stroke engine consult this mask to decide whether
// a pixel is inside the active selection. 0 means "ignore"; 255 means
// "operate here"; intermediate values let the future antialiased /
// feathered selection paths blend.
//
// Mirrors CanvasRenderTarget's shape on purpose so the renderer can
// treat the mask as another tracked image (transition_to / handle / view
// / extent / resize). What differs is the format (`R8_UNORM`) and the
// usage flags (no COLOR_ATTACHMENT until we ship a shader-based
// rasterizer — for now the rect-list rasterizer writes via
// vkCmdCopyBufferToImage).
//
// What does NOT live here:
//   - Rasterization. That sits in `compositor::SelectionRasterizer`,
//     which owns the staging buffer + records the upload. Keeping the
//     image and its rasterizer in separate types matches the
//     CanvasRenderTarget / LayerCompositor split: pure GPU resource on
//     one side, draw-recording on the other.
//
// Rationale: see docs/architecture/0021-selection-mask-gpu.md.

#include <cstdint>
#include <optional>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
// Image is held in std::optional so its full definition is needed.
#include "noted/engine/gpu/image.hpp"

namespace noted::gpu {

class Allocator;

struct SelectionMaskCreateInfo {
    VkExtent2D extent;
    // Format is pinned at R8_UNORM for v1. Exposing it as a parameter
    // would invite mismatches with the rasterizer's byte-per-texel
    // assumption — bump intentionally if/when a wider format lands.
};

// Move-only RAII wrapper around the selection mask image. Layout
// tracking matches CanvasRenderTarget: cached `{layout, access, stage}`
// triple, emitted as the src side of every transition_to() barrier.
class SelectionMask {
public:
    [[nodiscard]] static auto create(const Allocator& allocator,
                                     const SelectionMaskCreateInfo& info) -> Result<SelectionMask>;

    SelectionMask(SelectionMask&& other) noexcept = default;
    auto operator=(SelectionMask&& other) noexcept -> SelectionMask& = default;
    SelectionMask(const SelectionMask&) = delete;
    auto operator=(const SelectionMask&) -> SelectionMask& = delete;
    ~SelectionMask() = default;

    // Rebuild the underlying image at a new extent. Format is preserved.
    // No-op when extents match. Must not be called while the GPU may
    // still be reading or writing the mask — wait_idle, then resize.
    [[nodiscard]] auto resize(const Allocator& allocator, VkExtent2D new_extent) -> Result<void>;

    // Emit a sync2 image barrier from the cached state to the requested
    // one and update the cache.
    void transition_to(VkCommandBuffer cb,
                       VkImageLayout new_layout,
                       VkAccessFlags2 dst_access,
                       VkPipelineStageFlags2 dst_stage) noexcept;

    // Reset the cached layout state to UNDEFINED. Use after wait_idle
    // when the prior contents are no longer needed.
    void reset_layout_tracking() noexcept;

    [[nodiscard]] auto handle() const noexcept -> VkImage { return image_->handle(); }
    [[nodiscard]] auto view() const noexcept -> VkImageView { return image_->view(); }
    [[nodiscard]] auto format() const noexcept -> VkFormat { return image_->format(); }
    [[nodiscard]] auto extent() const noexcept -> VkExtent2D {
        return {image_->extent().width, image_->extent().height};
    }
    [[nodiscard]] auto current_layout() const noexcept -> VkImageLayout { return state_.layout; }

    // Bytes per texel for the mask's format. Single source of truth
    // shared with the rasterizer's staging-buffer sizing.
    [[nodiscard]] static constexpr auto bytes_per_texel() noexcept -> std::uint32_t { return 1; }

private:
    explicit SelectionMask(Image image) noexcept : image_{std::move(image)} {}

    struct State {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags2 access = 0;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    };

    std::optional<Image> image_;
    State state_{};
};

}  // namespace noted::gpu
