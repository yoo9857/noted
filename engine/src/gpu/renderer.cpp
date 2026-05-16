#include "noted/engine/gpu/renderer.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include "noted/engine/gpu/canvas_render_target.hpp"
#include "noted/engine/profile.hpp"

namespace noted::gpu {

namespace {

[[nodiscard]] auto create_binary_semaphore(VkDevice device, VkSemaphore& out) -> Result<void> {
    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (auto vr = vkCreateSemaphore(device, &sci, nullptr, &out); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateSemaphore(render_finished_per_image): "} +
                std::to_string(static_cast<int>(vr))));
    }
    return {};
}

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
    const Swapchain& swapchain,
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
    if (auto rs = r.rebuild_present_semaphores(device, swapchain); !rs) {
        return std::unexpected(std::move(rs).error());
    }
    return r;
}

auto Renderer::rebuild_present_semaphores(const Device& device,
                                          const Swapchain& swapchain) -> Result<void> {
    // Destroy any existing per-image semaphores first. They were not in use
    // at this point because rebuild is only called after wait_idle.
    for (auto s : render_finished_per_image_) {
        if (s != VK_NULL_HANDLE) {
            vkDestroySemaphore(owner_, s, nullptr);
        }
    }
    render_finished_per_image_.clear();

    const auto image_count = swapchain.images().size();
    render_finished_per_image_.reserve(image_count);
    for (std::size_t i = 0; i < image_count; ++i) {
        VkSemaphore s = VK_NULL_HANDLE;
        if (auto r = create_binary_semaphore(device.handle(), s); !r) {
            return std::unexpected(std::move(r).error());
        }
        render_finished_per_image_.push_back(s);
    }
    return {};
}

auto Renderer::rebind_swapchain(const Device& device,
                                const Swapchain& swapchain) -> Result<void> {
    // The per-image render_finished semaphore array must match the new
    // swapchain image count. Image-count changes are rare (mode change,
    // surface caps), but harmless to rebuild always — wait_idle was already
    // called by the swapchain recreate path.
    return rebuild_present_semaphores(device, swapchain);
}

auto Renderer::render_frame(
    const Device&    device,
    const Swapchain& swapchain,
    VkClearColorValue color) -> Result<void> {
    const auto slot_idx = frame_counter_ % frames_.size();
    auto& slot          = frames_[slot_idx];

    const VkFence     fence_h    = slot.sync.in_flight();
    const VkSemaphore acquire_h  = slot.sync.image_available();
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
    // The render_finished semaphore is picked per-acquired-image so the
    // present queue never sees a still-in-use binary semaphore.
    const VkSemaphore present_h = render_finished_per_image_[image_index];

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
    NOTED_PROFILE_ZONE_N("Renderer::render_frame_with");
    const auto slot_idx = frame_counter_ % frames_.size();
    auto& slot          = frames_[slot_idx];

    const VkFence       fence_h   = slot.sync.in_flight();
    const VkSemaphore   acquire_h = slot.sync.image_available();
    const VkSwapchainKHR sc_h     = swapchain.handle();
    const auto          extent    = swapchain.summary().extent;

    {
        NOTED_PROFILE_ZONE_N("waitForFences");
        if (auto vr = vkWaitForFences(owner_, 1, &fence_h, VK_TRUE, UINT64_MAX);
            vr != VK_SUCCESS) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::gpu_validation_failed,
                std::string{"vkWaitForFences: "} + std::to_string(static_cast<int>(vr))));
        }
    }

    std::uint32_t image_index = 0;
    VkResult      acq         = VK_SUCCESS;
    {
        NOTED_PROFILE_ZONE_N("acquireNextImage");
        acq = vkAcquireNextImageKHR(owner_, sc_h, UINT64_MAX,
                                    acquire_h, VK_NULL_HANDLE, &image_index);
    }
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
    // Per-image render_finished — picked after we know which image was acquired.
    const VkSemaphore present_h = render_finished_per_image_[image_index];

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

    {
        NOTED_PROFILE_ZONE_N("queueSubmit2");
        if (auto vr = vkQueueSubmit2(device.graphics_queue(), 1, &si, fence_h);
            vr != VK_SUCCESS) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::gpu_validation_failed,
                std::string{"vkQueueSubmit2: "} + std::to_string(static_cast<int>(vr))));
        }
    }

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &present_h;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc_h;
    pi.pImageIndices      = &image_index;

    VkResult pres = VK_SUCCESS;
    {
        NOTED_PROFILE_ZONE_N("queuePresent");
        pres = vkQueuePresentKHR(device.present_queue(), &pi);
    }
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

auto Renderer::render_with_canvas(
    const Device&            device,
    const Swapchain&         swapchain,
    CanvasRenderTarget&      canvas,
    const CanvasPassDesc&    canvas_pass,
    const SwapchainPassDesc& swapchain_pass) -> Result<void> {
    NOTED_PROFILE_ZONE_N("Renderer::render_with_canvas");
    const auto slot_idx = frame_counter_ % frames_.size();
    auto& slot          = frames_[slot_idx];

    const VkFence        fence_h     = slot.sync.in_flight();
    const VkSemaphore    acquire_h   = slot.sync.image_available();
    const VkSwapchainKHR sc_h        = swapchain.handle();
    const auto           sc_extent   = swapchain.summary().extent;
    const auto           cv_extent2d = canvas.extent();
    const VkExtent2D     cv_extent{cv_extent2d.width, cv_extent2d.height};

    {
        NOTED_PROFILE_ZONE_N("waitForFences");
        if (auto vr = vkWaitForFences(owner_, 1, &fence_h, VK_TRUE, UINT64_MAX);
            vr != VK_SUCCESS) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::gpu_validation_failed,
                std::string{"vkWaitForFences: "} + std::to_string(static_cast<int>(vr))));
        }
    }

    std::uint32_t image_index = 0;
    VkResult      acq         = VK_SUCCESS;
    {
        NOTED_PROFILE_ZONE_N("acquireNextImage");
        acq = vkAcquireNextImageKHR(owner_, sc_h, UINT64_MAX,
                                    acquire_h, VK_NULL_HANDLE, &image_index);
    }
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
    const VkSemaphore present_h = render_finished_per_image_[image_index];

    vkResetFences(owner_, 1, &fence_h);

    slot.cb.reset();
    if (auto r = slot.cb.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); !r) {
        return std::unexpected(std::move(r).error());
    }

    const auto sc_image = swapchain.images()[image_index];
    const auto sc_view  = swapchain.views()[image_index];

    // ---- Pass 1: Canvas ---------------------------------------------------
    canvas.transition_to(slot.cb.handle(),
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    {
        auto canvas_attach = canvas.color_attachment(canvas_pass.clear);
        VkRenderingInfo ri{};
        ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent    = cv_extent;
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &canvas_attach;
        vkCmdBeginRendering(slot.cb.handle(), &ri);

        VkViewport vp{};
        vp.x        = 0.0F;
        vp.y        = 0.0F;
        vp.width    = static_cast<float>(cv_extent.width);
        vp.height   = static_cast<float>(cv_extent.height);
        vp.minDepth = 0.0F;
        vp.maxDepth = 1.0F;
        vkCmdSetViewport(slot.cb.handle(), 0, 1, &vp);
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = cv_extent;
        vkCmdSetScissor(slot.cb.handle(), 0, 1, &scissor);

        if (canvas_pass.draw) {
            NOTED_PROFILE_ZONE_N("canvas_pass.draw");
            canvas_pass.draw(slot.cb.handle(), cv_extent);
        }
        vkCmdEndRendering(slot.cb.handle());
    }

    // Canvas → SHADER_READ for the composite pass that follows.
    canvas.transition_to(slot.cb.handle(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

    // ---- Pass 2: Swapchain (composite) -----------------------------------
    image_layout_transition(slot.cb.handle(), sc_image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        0, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);

    {
        VkRenderingAttachmentInfo sc_attach{};
        sc_attach.sType                = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        sc_attach.imageView            = sc_view;
        sc_attach.imageLayout          = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        sc_attach.loadOp               = VK_ATTACHMENT_LOAD_OP_CLEAR;
        sc_attach.storeOp              = VK_ATTACHMENT_STORE_OP_STORE;
        sc_attach.clearValue.color     = swapchain_pass.clear;

        VkRenderingInfo ri{};
        ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.extent    = sc_extent;
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &sc_attach;
        vkCmdBeginRendering(slot.cb.handle(), &ri);

        VkViewport vp{};
        vp.x        = 0.0F;
        vp.y        = 0.0F;
        vp.width    = static_cast<float>(sc_extent.width);
        vp.height   = static_cast<float>(sc_extent.height);
        vp.minDepth = 0.0F;
        vp.maxDepth = 1.0F;
        vkCmdSetViewport(slot.cb.handle(), 0, 1, &vp);
        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = sc_extent;
        vkCmdSetScissor(slot.cb.handle(), 0, 1, &scissor);

        if (swapchain_pass.draw) {
            NOTED_PROFILE_ZONE_N("swapchain_pass.draw");
            swapchain_pass.draw(slot.cb.handle(), sc_extent);
        }
        vkCmdEndRendering(slot.cb.handle());
    }

    image_layout_transition(slot.cb.handle(), sc_image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT);

    if (auto r = slot.cb.end(); !r) {
        return std::unexpected(std::move(r).error());
    }

    // ---- Submit ----------------------------------------------------------
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

    {
        NOTED_PROFILE_ZONE_N("queueSubmit2");
        if (auto vr = vkQueueSubmit2(device.graphics_queue(), 1, &si, fence_h);
            vr != VK_SUCCESS) {
            return std::unexpected(noted::make_error(
                noted::ErrorCode::gpu_validation_failed,
                std::string{"vkQueueSubmit2: "} + std::to_string(static_cast<int>(vr))));
        }
    }

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &present_h;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &sc_h;
    pi.pImageIndices      = &image_index;

    VkResult pres = VK_SUCCESS;
    {
        NOTED_PROFILE_ZONE_N("queuePresent");
        pres = vkQueuePresentKHR(device.present_queue(), &pi);
    }
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
      render_finished_per_image_(std::move(other.render_finished_per_image_)),
      frame_counter_(other.frame_counter_) {
    other.owner_         = VK_NULL_HANDLE;
    other.frame_counter_ = 0;
}

auto Renderer::operator=(Renderer&& other) noexcept -> Renderer& {
    if (this != &other) {
        destroy();
        owner_                     = other.owner_;
        frames_                    = std::move(other.frames_);
        render_finished_per_image_ = std::move(other.render_finished_per_image_);
        frame_counter_             = other.frame_counter_;
        other.owner_         = VK_NULL_HANDLE;
        other.frame_counter_ = 0;
    }
    return *this;
}

Renderer::~Renderer() { destroy(); }

void Renderer::destroy() noexcept {
    // Per-image semaphores live in plain VkSemaphore — destroy them manually.
    if (owner_ != VK_NULL_HANDLE) {
        for (auto s : render_finished_per_image_) {
            if (s != VK_NULL_HANDLE) {
                vkDestroySemaphore(owner_, s, nullptr);
            }
        }
    }
    render_finished_per_image_.clear();
    // The FrameSync / CommandBuffer / CommandPool destructors do their own
    // cleanup; clearing the vector destroys them in reverse-construction order.
    frames_.clear();
    owner_         = VK_NULL_HANDLE;
    frame_counter_ = 0;
}

}  // namespace noted::gpu
