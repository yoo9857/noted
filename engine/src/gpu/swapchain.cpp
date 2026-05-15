#include "noted/engine/gpu/swapchain.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace noted::gpu {

namespace {

[[nodiscard]] auto pick_surface_format(
    VkPhysicalDevice physical,
    VkSurfaceKHR     surface,
    bool             prefer_srgb) -> VkSurfaceFormatKHR {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());

    const VkFormat preferred = prefer_srgb ? VK_FORMAT_B8G8R8A8_SRGB
                                           : VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto& f : formats) {
        if (f.format == preferred &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    // Try the other 8-bit BGRA option.
    const VkFormat fallback = prefer_srgb ? VK_FORMAT_B8G8R8A8_UNORM
                                          : VK_FORMAT_B8G8R8A8_SRGB;
    for (const auto& f : formats) {
        if (f.format == fallback &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_UNDEFINED,
                                                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats.front();
}

[[nodiscard]] auto pick_present_mode(
    VkPhysicalDevice physical,
    VkSurfaceKHR     surface,
    bool             allow_mailbox,
    bool             force_fifo) -> VkPresentModeKHR {
    if (force_fifo) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data());

    auto has = [&](VkPresentModeKHR m) {
        return std::find(modes.begin(), modes.end(), m) != modes.end();
    };
    if (allow_mailbox && has(VK_PRESENT_MODE_MAILBOX_KHR)) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (has(VK_PRESENT_MODE_FIFO_RELAXED_KHR)) {
        return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;  // guaranteed by spec
}

[[nodiscard]] auto clamp_extent(VkExtent2D requested,
                                const VkSurfaceCapabilitiesKHR& caps) -> VkExtent2D {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    VkExtent2D e = requested;
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

[[nodiscard]] auto build(
    VkPhysicalDevice physical,
    VkDevice         device,
    VkSurfaceKHR     surface,
    VkExtent2D       window_extent,
    const SwapchainConfig& cfg,
    std::uint32_t    graphics_family,
    std::uint32_t    present_family,
    VkSwapchainKHR   old_swapchain,
    SwapchainSummary& out_summary)
    -> noted::Result<VkSwapchainKHR> {
    VkSurfaceCapabilitiesKHR caps{};
    if (auto vr = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkGetPhysicalDeviceSurfaceCapabilitiesKHR: "} + std::to_string(static_cast<int>(vr))));
    }

    const auto fmt    = pick_surface_format(physical, surface, cfg.prefer_srgb);
    const auto mode   = pick_present_mode(physical, surface, cfg.allow_mailbox, cfg.force_fifo);
    const auto extent = clamp_extent(window_extent, caps);

    std::uint32_t image_count = cfg.desired_image_count;
    image_count = std::max(image_count, caps.minImageCount);
    if (caps.maxImageCount > 0) {
        image_count = std::min(image_count, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = surface;
    sci.minImageCount    = image_count;
    sci.imageFormat      = fmt.format;
    sci.imageColorSpace  = fmt.colorSpace;
    sci.imageExtent      = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.preTransform     = caps.currentTransform;
    sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode      = mode;
    sci.clipped          = VK_TRUE;
    sci.oldSwapchain     = old_swapchain;

    const std::array<std::uint32_t, 2> queue_families{graphics_family, present_family};
    if (graphics_family != present_family) {
        sci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices   = queue_families.data();
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VkSwapchainKHR raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateSwapchainKHR(device, &sci, nullptr, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateSwapchainKHR: "} + std::to_string(static_cast<int>(vr))));
    }

    out_summary.color_format = fmt.format;
    out_summary.color_space  = fmt.colorSpace;
    out_summary.present_mode = mode;
    out_summary.extent       = extent;
    out_summary.image_count  = image_count;
    return raw;
}

[[nodiscard]] auto fetch_images_and_views(
    VkDevice                  device,
    VkSwapchainKHR            swapchain,
    VkFormat                  format,
    std::vector<VkImage>&     out_images,
    std::vector<VkImageView>& out_views) -> noted::Result<void> {
    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    out_images.assign(count, VK_NULL_HANDLE);
    vkGetSwapchainImagesKHR(device, swapchain, &count, out_images.data());

    out_views.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image    = out_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format   = format;
        vci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel   = 0;
        vci.subresourceRange.levelCount     = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount     = 1;

        VkImageView view = VK_NULL_HANDLE;
        if (auto vr = vkCreateImageView(device, &vci, nullptr, &view); vr != VK_SUCCESS) {
            // Destroy whatever we already made; the caller will see an
            // unbuilt Swapchain rather than partial state.
            for (auto v : out_views) {
                vkDestroyImageView(device, v, nullptr);
            }
            out_views.clear();
            out_images.clear();
            return std::unexpected(noted::make_error(
                noted::ErrorCode::gpu_validation_failed,
                std::string{"vkCreateImageView: "} + std::to_string(static_cast<int>(vr))));
        }
        out_views.push_back(view);
    }
    return {};
}

}  // namespace

auto Swapchain::create(
    const PhysicalDevice& physical,
    const Device&         device,
    const Surface&        surface,
    VkExtent2D            window_extent,
    const SwapchainConfig& cfg) -> Result<Swapchain> {
    Swapchain s;
    s.owner_           = device.handle();
    s.graphics_family_ = device.graphics_family();
    s.present_family_  = device.present_family();

    auto raw = build(physical.handle(), device.handle(), surface.handle(),
                     window_extent, cfg,
                     s.graphics_family_, s.present_family_,
                     VK_NULL_HANDLE, s.summary_);
    if (!raw) {
        return std::unexpected(std::move(raw).error());
    }
    s.handle_ = *raw;

    if (auto r = fetch_images_and_views(device.handle(), s.handle_,
                                         s.summary_.color_format,
                                         s.images_, s.views_); !r) {
        vkDestroySwapchainKHR(device.handle(), s.handle_, nullptr);
        s.handle_ = VK_NULL_HANDLE;
        return std::unexpected(std::move(r).error());
    }
    return s;
}

auto Swapchain::recreate(
    const PhysicalDevice& physical,
    const Surface&        surface,
    VkExtent2D            window_extent,
    const SwapchainConfig& cfg) -> Result<void> {
    const VkSwapchainKHR previous = handle_;
    auto raw = build(physical.handle(), owner_, surface.handle(),
                     window_extent, cfg,
                     graphics_family_, present_family_,
                     previous, summary_);
    if (!raw) {
        return std::unexpected(std::move(raw).error());
    }

    // Destroy views first, then the old swapchain.
    for (auto v : views_) {
        if (v != VK_NULL_HANDLE) {
            vkDestroyImageView(owner_, v, nullptr);
        }
    }
    views_.clear();
    if (previous != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(owner_, previous, nullptr);
    }

    handle_ = *raw;
    if (auto r = fetch_images_and_views(owner_, handle_, summary_.color_format,
                                         images_, views_); !r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : owner_(other.owner_),
      handle_(other.handle_),
      graphics_family_(other.graphics_family_),
      present_family_(other.present_family_),
      summary_(other.summary_),
      images_(std::move(other.images_)),
      views_(std::move(other.views_)) {
    other.owner_           = VK_NULL_HANDLE;
    other.handle_          = VK_NULL_HANDLE;
    other.graphics_family_ = UINT32_MAX;
    other.present_family_  = UINT32_MAX;
    other.summary_         = {};
}

auto Swapchain::operator=(Swapchain&& other) noexcept -> Swapchain& {
    if (this != &other) {
        destroy();
        owner_           = other.owner_;
        handle_          = other.handle_;
        graphics_family_ = other.graphics_family_;
        present_family_  = other.present_family_;
        summary_         = other.summary_;
        images_          = std::move(other.images_);
        views_           = std::move(other.views_);
        other.owner_           = VK_NULL_HANDLE;
        other.handle_          = VK_NULL_HANDLE;
        other.graphics_family_ = UINT32_MAX;
        other.present_family_  = UINT32_MAX;
        other.summary_         = {};
    }
    return *this;
}

Swapchain::~Swapchain() { destroy(); }

void Swapchain::destroy() noexcept {
    if (owner_ != VK_NULL_HANDLE) {
        for (auto v : views_) {
            if (v != VK_NULL_HANDLE) {
                vkDestroyImageView(owner_, v, nullptr);
            }
        }
        views_.clear();
        images_.clear();
        if (handle_ != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(owner_, handle_, nullptr);
            handle_ = VK_NULL_HANDLE;
        }
        owner_ = VK_NULL_HANDLE;
    }
}

}  // namespace noted::gpu
