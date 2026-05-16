#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/instance.hpp"

namespace noted::gpu {

struct QueueFamilyIndices {
    std::uint32_t graphics = UINT32_MAX;
    std::uint32_t compute = UINT32_MAX;
    std::uint32_t transfer = UINT32_MAX;

    [[nodiscard]] auto is_complete() const noexcept -> bool {
        return graphics != UINT32_MAX && compute != UINT32_MAX;
    }
};

// Picks a Vulkan physical device using a small scoring function:
//
//   discrete GPU          +1000
//   integrated GPU        +100
//   API >= requested      required
//   graphics + compute    required
//   max image dim 2D      ÷ 1024 added to score
//
// Returns the highest-scoring device that meets the requirements, or an
// error if none qualifies.
class PhysicalDevice {
public:
    [[nodiscard]] static auto select(const Instance& instance,
                                     std::uint32_t min_api_version = VK_API_VERSION_1_3)
        -> Result<PhysicalDevice>;

    [[nodiscard]] auto handle() const noexcept -> VkPhysicalDevice { return handle_; }
    [[nodiscard]] auto properties() const noexcept -> const VkPhysicalDeviceProperties& {
        return props_;
    }
    [[nodiscard]] auto queue_families() const noexcept -> const QueueFamilyIndices& {
        return queues_;
    }

private:
    PhysicalDevice() = default;

    VkPhysicalDevice handle_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props_{};
    QueueFamilyIndices queues_{};
};

}  // namespace noted::gpu
