#include "noted/engine/gpu/frame_sync.hpp"

#include <string>

namespace noted::gpu {

auto FrameSync::create(const Device& device) -> Result<FrameSync> {
    FrameSync s;
    s.owner_ = device.handle();

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    if (auto vr = vkCreateSemaphore(s.owner_, &sci, nullptr, &s.image_available_);
        vr != VK_SUCCESS) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::gpu_validation_failed,
                              std::string{"vkCreateSemaphore(image_available): "} +
                                  std::to_string(static_cast<int>(vr))));
    }

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // start signaled — first wait is a no-op
    if (auto vr = vkCreateFence(s.owner_, &fci, nullptr, &s.in_flight_); vr != VK_SUCCESS) {
        s.destroy();
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateFence(in_flight): "} + std::to_string(static_cast<int>(vr))));
    }
    return s;
}

FrameSync::FrameSync(FrameSync&& other) noexcept
    : owner_(other.owner_), image_available_(other.image_available_), in_flight_(other.in_flight_) {
    other.owner_ = VK_NULL_HANDLE;
    other.image_available_ = VK_NULL_HANDLE;
    other.in_flight_ = VK_NULL_HANDLE;
}

auto FrameSync::operator=(FrameSync&& other) noexcept -> FrameSync& {
    if (this != &other) {
        destroy();
        owner_ = other.owner_;
        image_available_ = other.image_available_;
        in_flight_ = other.in_flight_;
        other.owner_ = VK_NULL_HANDLE;
        other.image_available_ = VK_NULL_HANDLE;
        other.in_flight_ = VK_NULL_HANDLE;
    }
    return *this;
}

FrameSync::~FrameSync() {
    destroy();
}

void FrameSync::destroy() noexcept {
    if (owner_ == VK_NULL_HANDLE) {
        return;
    }
    if (image_available_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(owner_, image_available_, nullptr);
        image_available_ = VK_NULL_HANDLE;
    }
    if (in_flight_ != VK_NULL_HANDLE) {
        vkDestroyFence(owner_, in_flight_, nullptr);
        in_flight_ = VK_NULL_HANDLE;
    }
    owner_ = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
