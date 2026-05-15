#pragma once

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

// One frame's worth of sync primitives, indexed by the frame-in-flight slot.
//
//   image_available — signaled by vkAcquireNextImageKHR when the swapchain
//                     image is ready for rendering; waited on by the
//                     submit's COLOR_ATTACHMENT_OUTPUT stage.
//   in_flight       — fence signaled by the submit; the CPU waits on it
//                     before re-recording the next iteration of this slot.
//
// `render_finished` lives on the Renderer indexed by swapchain image, not
// here. Reason: the frame-in-flight count (typical: 2) is independent of
// the swapchain image count (typical: 3), and a render_finished semaphore
// signaled in frame N may still be in use by vkQueuePresentKHR while frame
// N+frames_in_flight tries to re-signal it on a different image. The
// Vulkan validation layer flags that pattern as a spec violation. The
// canonical fix is per-image render_finished — see Renderer.
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
    [[nodiscard]] auto in_flight() const noexcept -> VkFence         { return in_flight_; }

private:
    FrameSync() = default;
    void destroy() noexcept;

    VkDevice    owner_           = VK_NULL_HANDLE;
    VkSemaphore image_available_ = VK_NULL_HANDLE;
    VkFence     in_flight_       = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
