#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/physical_device.hpp"
#include "noted/engine/gpu/surface.hpp"

namespace noted::gpu {

struct SwapchainConfig {
    std::uint32_t desired_image_count = 3;  // triple-buffered
    bool prefer_srgb = true;                // SRGB > UNORM when both exist
    bool allow_mailbox = true;              // MAILBOX is low-latency vsync
    bool force_fifo = false;                // hard-vsync, ignores allow_mailbox
};

// Image format + extent the swapchain settled on.
struct SwapchainSummary {
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D extent = {0, 0};
    std::uint32_t image_count = 0;
};

// VkSwapchainKHR + owned image views.
//
// Format selection:
//   1. Match the first VK_FORMAT_B8G8R8A8_{S,U}RGB pair the surface supports
//      (prefer SRGB when prefer_srgb is true).
//   2. Otherwise take whatever the driver offers first.
//
// Present mode selection:
//   FIFO is always supported, used as a guaranteed fallback. MAILBOX is
//   preferred when allow_mailbox is true and the surface supports it.
//   FIFO_RELAXED is taken as a middle ground when MAILBOX is missing.
//   force_fifo forces FIFO regardless.
//
// recreate(extent) tears down and rebuilds for resize / out-of-date errors.
class Swapchain {
public:
    [[nodiscard]] static auto create(const PhysicalDevice& physical,
                                     const Device& device,
                                     const Surface& surface,
                                     VkExtent2D window_extent,
                                     const SwapchainConfig& cfg = {}) -> Result<Swapchain>;

    Swapchain(Swapchain&& other) noexcept;
    auto operator=(Swapchain&& other) noexcept -> Swapchain&;
    Swapchain(const Swapchain&) = delete;
    auto operator=(const Swapchain&) -> Swapchain& = delete;
    ~Swapchain();

    [[nodiscard]] auto handle() const noexcept -> VkSwapchainKHR { return handle_; }
    [[nodiscard]] auto summary() const noexcept -> const SwapchainSummary& { return summary_; }
    [[nodiscard]] auto images() const noexcept -> const std::vector<VkImage>& { return images_; }
    [[nodiscard]] auto views() const noexcept -> const std::vector<VkImageView>& { return views_; }

    // Tear down image views + swapchain (in that order) and rebuild with the
    // new window_extent. The Device must be idle before this is called.
    [[nodiscard]] auto recreate(const PhysicalDevice& physical,
                                const Surface& surface,
                                VkExtent2D window_extent,
                                const SwapchainConfig& cfg = {}) -> Result<void>;

private:
    Swapchain() = default;
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkSwapchainKHR handle_ = VK_NULL_HANDLE;
    std::uint32_t graphics_family_ = UINT32_MAX;
    std::uint32_t present_family_ = UINT32_MAX;
    SwapchainSummary summary_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
};

}  // namespace noted::gpu
