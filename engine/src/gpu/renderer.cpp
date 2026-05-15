#include "noted/engine/gpu/renderer.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace noted::gpu {

namespace {

[[nodiscard]] auto build_slot(const Device& device) -> Result<Renderer::FrameSlot> {
    auto pool = CommandPool::create(device, device.graphics_family());
    if (!pool) {
        return std::unexpected(std::move(pool).error());
    }
    auto cb = CommandBuffer::allocate(*pool);
    if (!cb) {
        return std::unexpected(std::move(cb).error());
    }
    auto sync = FrameSync::create(device);
    if (!sync) {
        return std::unexpected(std::move(sync).error());
    }
    return Renderer::FrameSlot{
        .pool = std::move(*pool),
        .cb   = std::move(*cb),
        .sync = std::move(*sync),
    };
}

void image_layout_transition(
    VkCommandBuffer cb,
    VkImage         image,
    VkImageLayout   old_layout,
    VkImageLayout   new_layout,
    VkAccessFlags2  src_access,
    VkAccessFlags2  dst_access,
    VkPipelineStageFlags2 src_stage,
    VkPipelineStageFlags2 dst_stage) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask    = src_stage;
    barrier.srcAccessMask   = src_access;
    barrier.dstStageMask    = dst_stage;
    barrier.dstAccessMask   = dst_access;
    barrier.oldLayout       = old_layout;
    barrier.newLayout       = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image           = image;
    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = 1;

    VkDependencyInfo dep{};
    dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers    = &barrier;
    vkCmdPipelineBarrier2(cb, &dep);
}

}  // namespace

auto Renderer::create(
    const Device&    device,
    const Swapchain& /*swapchain*/,
    std::uint32_t    frames_in_flight) -> Result<Renderer> {
    if (frames_in_flight == 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "frames_in_flight must be >= 1"));
    }

    Renderer r;
    r.owner_ = device.handle();
    r.frames_.reserve(frames_in_flight);
    for (std::uint32_t i = 0; i < frames_in_flight; ++i) {
        auto slot = build_slot(device);
        if (!slot) {
            return std::unexpected(std::move(slot).error());
        }
        r.frames_.push_back(std::move(*slot));
    }
    return r;
}

auto Renderer::rebind_swapchain(const Device& /*device*/,
                                const Swapchain& /*swapchain*/) -> Result<void> {
    // Currently the renderer doesn't hold any per-swapchain-image resources
    // (no framebuffers — we use dynamic_rendering on the swapchain image
    // views directly). When per-image resources arrive (e.g. depth attachments)
    // they will be rebuilt here.
    return {};
}

auto Renderer::render_frame(
    const Device&    device,
    const Swapchain& swapchain,
    VkClearColorValue color) -> Result<void> {
    const auto slot_idx = frame_counter_ % frames_.size();
    auto& slot          = frames_[slot_idx];

    const VkFence     fence_h    = slot.sync.in_flight();
    const VkSemaphore acquire_h  = slot.sync.image_available();
    const VkSemaphore present_h  = slot.sync.render_finished();
    const VkSwapchainKHR sc_h    = swapchain.handle();

    // 1. Wait for this slot's previous in-flight frame to finish.
    if (auto vr = vkWaitForFences(owner_, 1, &fence_h,
                                  VK_TRUE, UINT64_MAX); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkWaitForFences: "} + std::to_string(static_cast<int>(vr))));
    }

    // 2. Acquire the next swapchain image.
    std::uint32_t image_index = 0;
    auto acq = vkAcquireNextImageKHR(owner_, sc_h, UINT64_MAX,
                                     acquire_h, VK_NULL_HANDLE, &image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_swapchain_out_of_date,
            "vkAcquireNextImageKHR: VK_ERROR_OUT_OF_DATE_KHR"));
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkAcquireNextImageKHR: "} + std::to_string(static_cast<int>(acq))));
    }
    const bool suboptimal_acq = (acq == VK_SUBOPTIMAL_KHR);

    // Only reset the fence once we know we will submit.
    vkResetFences(owner_, 1, &fence_h);

    // 3. Record the clear.
    slot.cb.reset();
    if (auto r = slot.cb.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); !r) {
        return std::unexpected(std::move(r).error());
    }

    const auto image = swapchain.images()[image_index];

    image_layout_transition(slot.cb.handle(), image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_CLEAR_BIT);

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    vkCmdClearColorImage(slot.cb.handle(), image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);

    image_layout_transition(slot.cb.handle(), image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_TRANSFER_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    if (auto r = slot.cb.end(); !r) {
        return std::unexpected(std::move(r).error());
    }

    // 4. Submit.
    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = acquire_h;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;

    VkSemaphoreSubmitInfo signal_info{};
    signal_info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = present_h;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

    VkCommandBufferSubmitInfo cb_info{};
    cb_info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_info.commandBuffer = slot.cb.handle();

    VkSubmitInfo2 si{};
    si.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount   = 1;
    si.pWaitSemaphoreInfos      = &wait_info;
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cb_info;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos    = &signal_info;

    if (auto vr = vkQueueSubmit2(device.graphics_queue(), 1, &si,
                                 fence_h); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkQueueSubmit2: "} + std::to_string(static_cast<int>(vr))));
    }

    // 5. Present.
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &present_h;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc_h;
    pi.pImageIndices      = &image_index;

    const auto pres = vkQueuePresentKHR(device.present_queue(), &pi);
    ++frame_counter_;

    if (pres == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_swapchain_out_of_date,
            "vkQueuePresentKHR: VK_ERROR_OUT_OF_DATE_KHR"));
    }
    if (pres == VK_SUBOPTIMAL_KHR || suboptimal_acq) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_swapchain_suboptimal,
            "vkQueuePresentKHR: VK_SUBOPTIMAL_KHR"));
    }
    if (pres != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkQueuePresentKHR: "} + std::to_string(static_cast<int>(pres))));
    }
    return {};
}

auto Renderer::render_frame_with(
    const Device&       device,
    const Swapchain&    swapchain,
    VkClearColorValue   clear_color,
    const DrawCallback& draw_callback) -> Result<void> {
    const auto slot_idx = frame_counter_ % frames_.size();
    auto& slot          = frames_[slot_idx];

    const VkFence       fence_h   = slot.sync.in_flight();
    const VkSemaphore   acquire_h = slot.sync.image_available();
    const VkSemaphore   present_h = slot.sync.render_finished();
    const VkSwapchainKHR sc_h     = swapchain.handle();
    const auto          extent    = swapchain.summary().extent;

    if (auto vr = vkWaitForFences(owner_, 1, &fence_h, VK_TRUE, UINT64_MAX);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkWaitForFences: "} + std::to_string(static_cast<int>(vr))));
    }

    std::uint32_t image_index = 0;
    auto acq = vkAcquireNextImageKHR(owner_, sc_h, UINT64_MAX,
                                     acquire_h, VK_NULL_HANDLE, &image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_swapchain_out_of_date,
            "vkAcquireNextImageKHR: VK_ERROR_OUT_OF_DATE_KHR"));
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkAcquireNextImageKHR: "} + std::to_string(static_cast<int>(acq))));
    }
    const bool suboptimal_acq = (acq == VK_SUBOPTIMAL_KHR);

    vkResetFences(owner_, 1, &fence_h);

    slot.cb.reset();
    if (auto r = slot.cb.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); !r) {
        return std::unexpected(std::move(r).error());
    }

    const auto image = swapchain.images()[image_index];
    const auto view  = swapchain.views()[image_index];

    // UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL for rendering.
    image_layout_transition(slot.cb.handle(), image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    // vkCmdBeginRendering with load-op CLEAR (no separate ClearColorImage).
    VkRenderingAttachmentInfo color_attach{};
    color_attach.sType         = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color_attach.imageView     = view;
    color_attach.imageLayout   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attach.loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attach.storeOp       = VK_ATTACHMENT_STORE_OP_STORE;
    color_attach.clearValue.color = clear_color;

    VkRenderingInfo ri{};
    ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent    = extent;
    ri.layerCount           = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments    = &color_attach;
    vkCmdBeginRendering(slot.cb.handle(), &ri);

    // Default viewport + scissor matching the full framebuffer; the pipeline
    // is built with dynamic VIEWPORT + SCISSOR per ADR 0010.
    VkViewport vp{};
    vp.x        = 0.0F;
    vp.y        = 0.0F;
    vp.width    = static_cast<float>(extent.width);
    vp.height   = static_cast<float>(extent.height);
    vp.minDepth = 0.0F;
    vp.maxDepth = 1.0F;
    vkCmdSetViewport(slot.cb.handle(), 0, 1, &vp);
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(slot.cb.handle(), 0, 1, &scissor);

    if (draw_callback) {
        draw_callback(slot.cb.handle(), extent);
    }

    vkCmdEndRendering(slot.cb.handle());

    image_layout_transition(slot.cb.handle(), image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    if (auto r = slot.cb.end(); !r) {
        return std::unexpected(std::move(r).error());
    }

    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = acquire_h;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info{};
    signal_info.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = present_h;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;

    VkCommandBufferSubmitInfo cb_info{};
    cb_info.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cb_info.commandBuffer = slot.cb.handle();

    VkSubmitInfo2 si{};
    si.sType                    = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    si.waitSemaphoreInfoCount   = 1;
    si.pWaitSemaphoreInfos      = &wait_info;
    si.commandBufferInfoCount   = 1;
    si.pCommandBufferInfos      = &cb_info;
    si.signalSemaphoreInfoCount = 1;
    si.pSignalSemaphoreInfos    = &signal_info;

    if (auto vr = vkQueueSubmit2(device.graphics_queue(), 1, &si, fence_h);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkQueueSubmit2: "} + std::to_string(static_cast<int>(vr))));
    }

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &present_h;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc_h;
    pi.pImageIndices      = &image_index;

    const auto pres = vkQueuePresentKHR(device.present_queue(), &pi);
    ++frame_counter_;

    if (pres == VK_ERROR_OUT_OF_DATE_KHR) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_swapchain_out_of_date,
            "vkQueuePresentKHR: VK_ERROR_OUT_OF_DATE_KHR"));
    }
    if (pres == VK_SUBOPTIMAL_KHR || suboptimal_acq) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_swapchain_suboptimal,
            "vkQueuePresentKHR: VK_SUBOPTIMAL_KHR"));
    }
    if (pres != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkQueuePresentKHR: "} + std::to_string(static_cast<int>(pres))));
    }
    return {};
}

Renderer::Renderer(Renderer&& other) noexcept
    : owner_(other.owner_),
      frames_(std::move(other.frames_)),
      frame_counter_(other.frame_counter_) {
    other.owner_         = VK_NULL_HANDLE;
    other.frame_counter_ = 0;
}

auto Renderer::operator=(Renderer&& other) noexcept -> Renderer& {
    if (this != &other) {
        destroy();
        owner_         = other.owner_;
        frames_        = std::move(other.frames_);
        frame_counter_ = other.frame_counter_;
        other.owner_         = VK_NULL_HANDLE;
        other.frame_counter_ = 0;
    }
    return *this;
}

Renderer::~Renderer() { destroy(); }

void Renderer::destroy() noexcept {
    // The FrameSync / CommandBuffer / CommandPool destructors do their own
    // cleanup; clearing the vector destroys them in reverse-construction order.
    frames_.clear();
    owner_         = VK_NULL_HANDLE;
    frame_counter_ = 0;
}

}  // namespace noted::gpu
