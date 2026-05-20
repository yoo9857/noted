#pragma once

// StrokeTarget — the offscreen image the stroke engine renders ink into.
//
// Lives ALONGSIDE `CanvasRenderTarget`, not inside it. The product
// requirement that motivates the split:
//
//   - Eraser strokes must remove ink without removing the page
//     background pattern below. A single canvas target mixes paper,
//     layers, and ink into one image — destination-out blending on
//     that target also punches through paper and layers. Two
//     separate targets, composited later, give the eraser the right
//     surface to operate on.
//
//   - Future raster brushes (the Photoshop side — image edits, photo
//     retouching) write into this same surface with arbitrary RGBA.
//     Keeping the stroke layer's identity distinct from the canvas
//     makes that path straightforward.
//
//   - Per-layer ink (eventually: ink that lives on a specific
//     `LayerGraph` node) becomes a per-layer stroke target consumed
//     by the compositor.
//
// Structurally identical to `CanvasRenderTarget` today (R8G8B8A8_UNORM,
// COLOR_ATTACHMENT | SAMPLED | TRANSFER_DST, per-instance layout
// tracking). Kept as a separate type for **semantic** clarity at the
// call sites that distinguish "the canvas the swapchain composites"
// from "the layer strokes accumulate into." If they diverge — e.g.
// the stroke target wants float16 for proper alpha math during
// erase — only this type changes.
//
// Rationale: see docs/architecture/0031-tool-state-machine.md.

#include <cstdint>
#include <optional>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/image.hpp"

namespace noted::gpu {

class Allocator;

struct StrokeTargetCreateInfo {
    VkExtent2D extent;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

class StrokeTarget {
public:
    [[nodiscard]] static auto create(const Allocator& allocator,
                                     const StrokeTargetCreateInfo& info) -> Result<StrokeTarget>;

    StrokeTarget(StrokeTarget&& other) noexcept = default;
    auto operator=(StrokeTarget&& other) noexcept -> StrokeTarget& = default;
    StrokeTarget(const StrokeTarget&) = delete;
    auto operator=(const StrokeTarget&) -> StrokeTarget& = delete;
    ~StrokeTarget() = default;

    [[nodiscard]] auto resize(const Allocator& allocator, VkExtent2D new_extent) -> Result<void>;

    void transition_to(VkCommandBuffer cb,
                       VkImageLayout new_layout,
                       VkAccessFlags2 dst_access,
                       VkPipelineStageFlags2 dst_stage) noexcept;

    [[nodiscard]] auto color_attachment(VkClearColorValue clear,
                                        VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                        VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE)
        const noexcept -> VkRenderingAttachmentInfo;

    void reset_layout_tracking() noexcept;

    [[nodiscard]] auto handle() const noexcept -> VkImage { return image_->handle(); }
    [[nodiscard]] auto view() const noexcept -> VkImageView { return image_->view(); }
    [[nodiscard]] auto format() const noexcept -> VkFormat { return image_->format(); }
    [[nodiscard]] auto extent() const noexcept -> VkExtent2D {
        return {image_->extent().width, image_->extent().height};
    }
    [[nodiscard]] auto current_layout() const noexcept -> VkImageLayout { return state_.layout; }

private:
    explicit StrokeTarget(Image image) noexcept : image_{std::move(image)} {}

    struct State {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags2 access = 0;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    };

    std::optional<Image> image_;
    State state_{};
};

}  // namespace noted::gpu
