#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/surface.hpp"

namespace noted::gpu {

struct DeviceCreateInfo {
    // Caller-provided extensions are appended to the engine-required
    // baseline (VK_KHR_swapchain).
    std::span<const char* const> extra_extensions = {};

    // Vulkan 1.3 features the engine assumes. Backends/tests can disable
    // these temporarily, but the renderer relies on dynamic_rendering and
    // synchronization2 across the codebase.
    bool enable_dynamic_rendering = true;
    bool enable_synchronization2  = true;
};

// Logical device + queue handles.
//
// Picks queues with these rules:
//   - graphics_queue is the queue from the graphics family.
//   - present_queue is the queue from the *first* family that supports
//     presenting to the given Surface. If the graphics family already
//     supports present (the common case), the two handles point at the
//     same VkQueue.
//
// Move-only RAII. vkDestroyDevice is called from the destructor.
class Device {
public:
    [[nodiscard]] static auto create(
        const PhysicalDevice& physical,
        const Surface&        surface,
        const DeviceCreateInfo& info = {}) -> Result<Device>;

    Device(Device&& other) noexcept;
    auto operator=(Device&& other) noexcept -> Device&;
    Device(const Device&) = delete;
    auto operator=(const Device&) -> Device& = delete;
    ~Device();

    [[nodiscard]] auto handle() const noexcept -> VkDevice { return handle_; }
    [[nodiscard]] auto graphics_queue() const noexcept -> VkQueue { return graphics_queue_; }
    [[nodiscard]] auto present_queue() const noexcept -> VkQueue { return present_queue_; }
    [[nodiscard]] auto graphics_family() const noexcept -> std::uint32_t { return graphics_family_; }
    [[nodiscard]] auto present_family() const noexcept -> std::uint32_t { return present_family_; }

    // Blocks the calling thread until every queue on this device is idle.
    // Used before destroying resources that the GPU might still reference.
    void wait_idle() const noexcept;

private:
    Device() = default;
    void destroy() noexcept;

    VkDevice      handle_           = VK_NULL_HANDLE;
    VkQueue       graphics_queue_   = VK_NULL_HANDLE;
    VkQueue       present_queue_    = VK_NULL_HANDLE;
    std::uint32_t graphics_family_  = UINT32_MAX;
    std::uint32_t present_family_   = UINT32_MAX;
};

}  // namespace noted::gpu
