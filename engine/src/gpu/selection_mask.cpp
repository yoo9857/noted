#include "noted/engine/gpu/selection_mask.hpp"

#include <utility>

#include "noted/engine/gpu/allocator.hpp"

namespace noted::gpu {

namespace {

// SAMPLED so the compositor's fragment shader can read it; TRANSFER_DST
// because the v1 rasterizer is buffer-to-image copy. When a shader
// rasterizer lands, add COLOR_ATTACHMENT_BIT here (and bump the ADR).
constexpr VkImageUsageFlags kMaskUsage =
    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

constexpr VkFormat kMaskFormat = VK_FORMAT_R8_UNORM;

}  // namespace

auto SelectionMask::create(const Allocator& allocator,
                           const SelectionMaskCreateInfo& info) -> Result<SelectionMask> {
    if (info.extent.width == 0 || info.extent.height == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "SelectionMask::create: extent has a zero dimension"));
    }

    auto img = Image::create(allocator,
                             ImageCreateInfo{
                                 .format = kMaskFormat,
                                 .extent = VkExtent3D{info.extent.width, info.extent.height, 1U},
                                 .usage = kMaskUsage,
                             });
    if (!img) {
        return std::unexpected(std::move(img).error());
    }

    return SelectionMask{std::move(*img)};
}

auto SelectionMask::resize(const Allocator& allocator, VkExtent2D new_extent) -> Result<void> {
    if (!image_.has_value()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "SelectionMask::resize: mask is disengaged"));
    }
    if (new_extent.width == 0 || new_extent.height == 0) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "SelectionMask::resize: extent has a zero dimension"));
    }
    const auto cur = extent();
    if (cur.width == new_extent.width && cur.height == new_extent.height) {
        return {};
    }

    auto fresh = Image::create(allocator,
                               ImageCreateInfo{
                                   .format = image_->format(),
                                   .extent = VkExtent3D{new_extent.width, new_extent.height, 1U},
                                   .usage = kMaskUsage,
                               });
    if (!fresh) {
        return std::unexpected(std::move(fresh).error());
    }

    image_.emplace(std::move(*fresh));
    state_ = State{};
    return {};
}

void SelectionMask::transition_to(VkCommandBuffer cb,
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

void SelectionMask::reset_layout_tracking() noexcept {
    state_ = State{};
}

}  // namespace noted::gpu
