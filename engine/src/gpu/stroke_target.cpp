#include "noted/engine/gpu/stroke_target.hpp"

#include <utility>

#include "noted/engine/gpu/allocator.hpp"

namespace noted::gpu {

namespace {

// Same usage flags as `CanvasRenderTarget` — the strokes target is
// rendered into (COLOR_ATTACHMENT), sampled by the overlay composite
// pass (SAMPLED), and reserved for future blit-in operations like
// importing an existing ink layer (TRANSFER_DST).
constexpr VkImageUsageFlags kStrokeTargetUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                 VK_IMAGE_USAGE_SAMPLED_BIT |
                                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;

}  // namespace

auto StrokeTarget::create(const Allocator& allocator,
                          const StrokeTargetCreateInfo& info) -> Result<StrokeTarget> {
    if (info.extent.width == 0 || info.extent.height == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "StrokeTarget::create: extent has a zero dimension"));
    }

    auto img = Image::create(allocator,
                             ImageCreateInfo{
                                 .format = info.format,
                                 .extent = VkExtent3D{info.extent.width, info.extent.height, 1U},
                                 .usage = kStrokeTargetUsage,
                             });
    if (!img) {
        return std::unexpected(std::move(img).error());
    }

    return StrokeTarget{std::move(*img)};
}

auto StrokeTarget::resize(const Allocator& allocator, VkExtent2D new_extent) -> Result<void> {
    if (!image_.has_value()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "StrokeTarget::resize: target is disengaged"));
    }
    if (new_extent.width == 0 || new_extent.height == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "StrokeTarget::resize: extent has a zero dimension"));
    }
    const auto cur = extent();
    if (cur.width == new_extent.width && cur.height == new_extent.height) {
        return {};
    }

    // Build the new image BEFORE tearing down the old one so a failure
    // leaves the target in its prior valid state.
    auto fresh = Image::create(allocator,
                               ImageCreateInfo{
                                   .format = image_->format(),
                                   .extent = VkExtent3D{new_extent.width, new_extent.height, 1U},
                                   .usage = kStrokeTargetUsage,
                               });
    if (!fresh) {
        return std::unexpected(std::move(fresh).error());
    }

    image_.emplace(std::move(*fresh));
    state_ = State{};  // contents discarded
    return {};
}

void StrokeTarget::transition_to(VkCommandBuffer cb,
                                 VkImageLayout new_layout,
                                 VkAccessFlags2 dst_access,
                                 VkPipelineStageFlags2 dst_stage) noexcept {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = state_.stage;
    b.srcAccessMask = state_.access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = state_.layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image_->handle();
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkDependencyInfo di{};
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &di);

    state_.layout = new_layout;
    state_.access = dst_access;
    state_.stage = dst_stage;
}

auto StrokeTarget::color_attachment(VkClearColorValue clear,
                                    VkAttachmentLoadOp load_op,
                                    VkAttachmentStoreOp store_op) const noexcept
    -> VkRenderingAttachmentInfo {
    VkRenderingAttachmentInfo a{};
    a.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    a.imageView = image_->view();
    a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    a.loadOp = load_op;
    a.storeOp = store_op;
    a.clearValue.color = clear;
    return a;
}

void StrokeTarget::reset_layout_tracking() noexcept {
    state_ = State{};
}

}  // namespace noted::gpu
