#include "noted/engine/gpu/upload.hpp"

#include <string>
#include <utility>

#include "noted/engine/gpu/buffer.hpp"
#include "noted/engine/gpu/command_buffer.hpp"
#include "noted/engine/gpu/command_pool.hpp"

namespace noted::gpu {

namespace {

void barrier(VkCommandBuffer cb,
             VkImage image,
             VkImageAspectFlags aspect,
             std::uint32_t mip,
             std::uint32_t base_layer,
             std::uint32_t layer_count,
             VkImageLayout old_layout,
             VkImageLayout new_layout,
             VkAccessFlags2 src_access,
             VkAccessFlags2 dst_access,
             VkPipelineStageFlags2 src_stage,
             VkPipelineStageFlags2 dst_stage) {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = src_stage;
    b.srcAccessMask = src_access;
    b.dstStageMask = dst_stage;
    b.dstAccessMask = dst_access;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.baseMipLevel = mip;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = base_layer;
    b.subresourceRange.layerCount = layer_count;

    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cb, &dep);
}

}  // namespace

auto upload_image_pixels(const Allocator& allocator,
                         const Device& device,
                         Image& dst,
                         std::span<const std::byte> pixels,
                         const UploadImageInfo& info) -> Result<void> {
    if (info.queue == VK_NULL_HANDLE || info.queue_family == UINT32_MAX) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "upload_image_pixels: UploadImageInfo.queue and queue_family must be set"));
    }
    if (pixels.empty()) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "upload_image_pixels: pixels span is empty"));
    }

    // 1. Staging buffer.
    auto staging = Buffer::create(allocator,
                                  BufferCreateInfo{
                                      .size = pixels.size_bytes(),
                                      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      .memory = MemoryUsage::cpu_to_gpu,
                                      .persistent_map = true,
                                  });
    if (!staging) {
        return std::unexpected(std::move(staging).error());
    }
    if (auto r = staging->write(pixels); !r) {
        return std::unexpected(std::move(r).error());
    }

    // 2. One-shot command pool + buffer.
    auto pool =
        CommandPool::create(device, info.queue_family, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    if (!pool) {
        return std::unexpected(std::move(pool).error());
    }
    auto cb = CommandBuffer::allocate(*pool);
    if (!cb) {
        return std::unexpected(std::move(cb).error());
    }
    if (auto r = cb->begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); !r) {
        return std::unexpected(std::move(r).error());
    }

    // 3. Layout transition → TRANSFER_DST.
    barrier(cb->handle(),
            dst.handle(),
            info.aspect,
            info.mip_level,
            info.base_array_layer,
            info.layer_count,
            info.current_layout,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT);

    // 4. Copy staging → image.
    VkBufferImageCopy2 region{};
    region.sType = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;  // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = info.aspect;
    region.imageSubresource.mipLevel = info.mip_level;
    region.imageSubresource.baseArrayLayer = info.base_array_layer;
    region.imageSubresource.layerCount = info.layer_count;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = dst.extent();

    VkCopyBufferToImageInfo2 copy{};
    copy.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2;
    copy.srcBuffer = staging->handle();
    copy.dstImage = dst.handle();
    copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copy.regionCount = 1;
    copy.pRegions = &region;
    vkCmdCopyBufferToImage2(cb->handle(), &copy);

    // 5. Layout transition → final_layout (shader read by default).
    barrier(cb->handle(),
            dst.handle(),
            info.aspect,
            info.mip_level,
            info.base_array_layer,
            info.layer_count,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            info.final_layout,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    if (auto r = cb->end(); !r) {
        return std::unexpected(std::move(r).error());
    }

    // 6. Submit with a one-shot fence and wait.
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence done = VK_NULL_HANDLE;
    if (auto vr = vkCreateFence(device.handle(), &fci, nullptr, &done); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateFence(upload): "} + std::to_string(static_cast<int>(vr))));
    }

    VkCommandBufferSubmitInfo cb_info{};
    cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_info.commandBuffer = cb->handle();

    VkSubmitInfo2 si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.commandBufferInfoCount = 1;
    si.pCommandBufferInfos = &cb_info;

    if (auto vr = vkQueueSubmit2(info.queue, 1, &si, done); vr != VK_SUCCESS) {
        vkDestroyFence(device.handle(), done, nullptr);
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkQueueSubmit2(upload): "} + std::to_string(static_cast<int>(vr))));
    }

    (void) vkWaitForFences(device.handle(), 1, &done, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device.handle(), done, nullptr);
    return {};
}

}  // namespace noted::gpu
