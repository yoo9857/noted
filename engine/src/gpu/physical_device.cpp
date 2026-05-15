#include "noted/engine/gpu/physical_device.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace noted::gpu {

namespace {

[[nodiscard]] auto find_queue_families(VkPhysicalDevice dev) -> QueueFamilyIndices {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &count, qprops.data());

    QueueFamilyIndices out;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto& q = qprops[i];
        if ((q.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && out.graphics == UINT32_MAX) {
            out.graphics = i;
        }
        if ((q.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0 && out.compute == UINT32_MAX) {
            out.compute = i;
        }
        if ((q.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0 && out.transfer == UINT32_MAX) {
            out.transfer = i;
        }
    }
    if (out.transfer == UINT32_MAX) {
        out.transfer = out.graphics;
    }
    return out;
}

[[nodiscard]] auto score(const VkPhysicalDeviceProperties& props) -> std::uint64_t {
    std::uint64_t s = 0;
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   s += 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: s += 100;  break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    s += 50;   break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            s += 10;   break;
        default:                                                break;
    }
    s += props.limits.maxImageDimension2D / 1024U;
    return s;
}

}  // namespace

auto PhysicalDevice::select(const Instance& instance, std::uint32_t min_api_version)
    -> Result<PhysicalDevice> {
    std::uint32_t count = 0;
    if (auto vr = vkEnumeratePhysicalDevices(instance.handle(), &count, nullptr);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkEnumeratePhysicalDevices(count) failed: "} + std::to_string(vr)));
    }
    if (count == 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_device_lost,
            "no Vulkan physical devices present"));
    }

    std::vector<VkPhysicalDevice> devices(count);
    if (auto vr = vkEnumeratePhysicalDevices(instance.handle(), &count, devices.data());
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkEnumeratePhysicalDevices failed: "} + std::to_string(vr)));
    }

    PhysicalDevice best;
    std::uint64_t  best_score = 0;
    for (auto d : devices) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        if (p.apiVersion < min_api_version) {
            continue;
        }
        auto q = find_queue_families(d);
        if (!q.is_complete()) {
            continue;
        }
        const auto s = score(p);
        if (s > best_score) {
            best_score    = s;
            best.handle_  = d;
            best.props_   = p;
            best.queues_  = q;
        }
    }

    if (best.handle_ == VK_NULL_HANDLE) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_device_lost,
            "no Vulkan device meets API/queue requirements"));
    }
    return best;
}

}  // namespace noted::gpu
