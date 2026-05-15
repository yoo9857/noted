#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/allocator.hpp"

namespace noted::gpu {

struct ImageCreateInfo {
    VkFormat              format       = VK_FORMAT_R8G8B8A8_UNORM;
    VkExtent3D            extent       = {1, 1, 1};
    VkImageType           type         = VK_IMAGE_TYPE_2D;
    VkImageViewType       view_type    = VK_IMAGE_VIEW_TYPE_2D;
    std::uint32_t         mip_levels   = 1;
    std::uint32_t         array_layers = 1;
    VkSampleCountFlagBits samples      = VK_SAMPLE_COUNT_1_BIT;
    VkImageTiling         tiling       = VK_IMAGE_TILING_OPTIMAL;
    VkImageUsageFlags     usage        = VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageAspectFlags    aspect       = VK_IMAGE_ASPECT_COLOR_BIT;
};

// VkImage + VkImageView + VmaAllocation. Move-only RAII.
//
// The view is built immediately with the aspect mask and view_type the
// create-info requested. For depth+stencil images set
// aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT.
class Image {
public:
    [[nodiscard]] static auto create(const Allocator& allocator,
                                     const ImageCreateInfo& info) -> Result<Image>;

    Image(Image&& other) noexcept;
    auto operator=(Image&& other) noexcept -> Image&;
    Image(const Image&) = delete;
    auto operator=(const Image&) -> Image& = delete;
    ~Image();

    [[nodiscard]] auto handle() const noexcept -> VkImage { return handle_; }
    [[nodiscard]] auto view() const noexcept   -> VkImageView { return view_; }
    [[nodiscard]] auto format() const noexcept -> VkFormat { return format_; }
    [[nodiscard]] auto extent() const noexcept -> VkExtent3D { return extent_; }
    [[nodiscard]] auto allocation() const noexcept -> VmaAllocation { return allocation_; }

private:
    Image() = default;
    void destroy() noexcept;

    VmaAllocator  owner_      = nullptr;
    VkDevice      device_     = VK_NULL_HANDLE;
    VkImage       handle_     = VK_NULL_HANDLE;
    VkImageView   view_       = VK_NULL_HANDLE;
    VmaAllocation allocation_ = nullptr;
    VkFormat      format_     = VK_FORMAT_UNDEFINED;
    VkExtent3D    extent_     = {0, 0, 0};
};

}  // namespace noted::gpu
