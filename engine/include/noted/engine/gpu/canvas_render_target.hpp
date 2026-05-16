#pragma once

// Offscreen canvas render target.
//
// The canvas is the single offscreen image every layer / stroke / filter pass
// renders into. The swapchain composites it onto the surface in a separate
// pass. Separating canvas-space rendering from surface-space presentation:
//
//   - decouples document resolution from the window — a 4K document can
//     render into a 4K canvas while the window shows a 1080p downsample
//     (future work; for v1 they match).
//   - lets us run post-effects (color correction, vignette, dither)
//     between canvas and swapchain without re-recording the layer graph.
//   - keeps the swapchain pass cheap and predictable — exactly one
//     fullscreen quad, no document-dependent state.
//
// This first cut keeps it simple:
//   - format: VK_FORMAT_R8G8B8A8_UNORM. Sufficient for sRGB display.
//     Switch to R16G16B16A16_SFLOAT in a follow-up when HDR compositing
//     (Photoshop "Linear Light", proper alpha math) lands.
//   - usage: COLOR_ATTACHMENT | SAMPLED | TRANSFER_DST (so we can also
//     blit thumbnails, snapshots, or imported images directly into the
//     canvas).
//   - layout: the canvas tracks its own current layout/access/stage and
//     emits the minimum-correct sync2 barrier on transition_to(). This
//     keeps callers (Renderer, future RenderGraph) honest without
//     forcing them to remember the prior frame's state.
//
// Rationale: see docs/architecture/0014-canvas-render-target.md.

#include <cstdint>
#include <optional>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
// Image is held in std::optional so its full definition is needed.
// Allocator only appears as `const Allocator&` — forward declared below to
// keep this header from transitively pulling vk_mem_alloc.h into every
// consumer.
#include "noted/engine/gpu/image.hpp"

namespace noted::gpu {

class Allocator;

struct CanvasCreateInfo {
    VkExtent2D extent;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

// Move-only RAII wrapper around a color render target.
//
// Layout tracking: the canvas is born in VK_IMAGE_LAYOUT_UNDEFINED. Each
// transition_to() emits the right sync2 barrier and updates the cached
// state. Tracking is intentionally per-instance, not global — once we
// add multiple concurrently-recorded canvases (compositor scratch,
// thumbnails) the tracking stays local to each.
class CanvasRenderTarget {
public:
    [[nodiscard]] static auto create(const Allocator& allocator,
                                     const CanvasCreateInfo& info) -> Result<CanvasRenderTarget>;

    CanvasRenderTarget(CanvasRenderTarget&& other) noexcept = default;
    auto operator=(CanvasRenderTarget&& other) noexcept -> CanvasRenderTarget& = default;
    CanvasRenderTarget(const CanvasRenderTarget&) = delete;
    auto operator=(const CanvasRenderTarget&) -> CanvasRenderTarget& = delete;
    ~CanvasRenderTarget() = default;

    // Tear down the underlying image and rebuild at a new extent. The
    // format is preserved. If new_extent equals the current extent this
    // is a no-op (returns success without touching the image).
    //
    // Must NOT be called while the GPU may still be reading or writing
    // the canvas — wait_idle, then resize.
    [[nodiscard]] auto resize(const Allocator& allocator, VkExtent2D new_extent) -> Result<void>;

    // Emit a sync2 barrier from the cached state to the requested one,
    // then update the cache. No-op if already in the requested layout
    // AND the dst stage/access set is a superset of the cached source
    // (rare — usually one or the other changes between passes).
    void transition_to(VkCommandBuffer cb,
                       VkImageLayout new_layout,
                       VkAccessFlags2 dst_access,
                       VkPipelineStageFlags2 dst_stage) noexcept;

    // Build a VkRenderingAttachmentInfo for vkCmdBeginRendering. Caller
    // must have called transition_to(COLOR_ATTACHMENT_OPTIMAL, ...) first.
    [[nodiscard]] auto color_attachment(VkClearColorValue clear,
                                        VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                        VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE)
        const noexcept -> VkRenderingAttachmentInfo;

    // Reset the cached layout state to UNDEFINED. Use after a resize()
    // or after the device has been wait_idle'd and any prior contents
    // are no longer needed.
    void reset_layout_tracking() noexcept;

    [[nodiscard]] auto handle() const noexcept -> VkImage { return image_->handle(); }
    [[nodiscard]] auto view() const noexcept -> VkImageView { return image_->view(); }
    [[nodiscard]] auto format() const noexcept -> VkFormat { return image_->format(); }
    [[nodiscard]] auto extent() const noexcept -> VkExtent2D {
        return {image_->extent().width, image_->extent().height};
    }
    [[nodiscard]] auto current_layout() const noexcept -> VkImageLayout { return state_.layout; }

private:
    explicit CanvasRenderTarget(Image image) noexcept : image_{std::move(image)} {}

    struct State {
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkAccessFlags2 access = 0;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    };

    // optional so the type stays default-move-constructible (Image's
    // own default ctor is private). After create() succeeds image_ is
    // always engaged; resize() re-engages it with a new Image.
    std::optional<Image> image_;
    State state_{};
};

}  // namespace noted::gpu
