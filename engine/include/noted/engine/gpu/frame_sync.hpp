#pragma once

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

// One frame's worth of sync primitives.
//
//   image_available — signaled by vkAcquireNextImageKHR when the swapchain
//                     image is ready for rendering; waited on by the
//                     submit's COLOR_ATTACHMENT_OUTPUT stage.
//   render_finished — signaled by the submit; waited on by vkQueuePresentKHR.
//   in_flight       — fence signaled by the submit; the CPU waits on it
//                     before re-recording the next iteration of this slot.
//
// The renderer holds N FrameSync objects (one per frame-in-flight) and
// rotates through them.
class FrameSync {
public:
    [[nodiscard]] static auto create(const Device& device) -> Result<FrameSync>;

    FrameSync(FrameSync&& other) noexcept;
    auto operator=(FrameSync&& other) noexcept -> FrameSync&;
    FrameSync(const FrameSync&) = delete;
    auto operator=(const FrameSync&) -> FrameSync& = delete;
    ~FrameSync();

    [[nodiscard]] auto image_available() const noexcept -> VkSemaphore { return image_available_; }
    [[nodiscard]] auto render_finished() const noexcept -> VkSemaphore { return render_finished_; }
    [[nodiscard]] auto in_flight() const noexcept -> VkFence         { return in_flight_; }

private:
    FrameSync() = default;
    void destroy() noexcept;

    VkDevice    owner_           = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkSemaphore render_finished_ = VK_NULL_HANDLE;
    VkFence     in_flight_       = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
