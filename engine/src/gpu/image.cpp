#include "noted/engine/gpu/image.hpp"

#include <string>

namespace noted::gpu {

auto Image::create(const Allocator& allocator, const ImageCreateInfo& info) -> Result<Image> {
    if (info.extent.width == 0 || info.extent.height == 0 || info.extent.depth == 0) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "Image::create: extent has a zero dimension"));
    }

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = info.type;
    ici.format = info.format;
    ici.extent = info.extent;
    ici.mipLevels = info.mip_levels;
    ici.arrayLayers = info.array_layers;
    ici.samples = info.samples;
    ici.tiling = info.tiling;
    ici.usage = info.usage;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage raw_img = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    if (auto vr = vmaCreateImage(allocator.handle(), &ici, &aci, &raw_img, &alloc, nullptr);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_out_of_memory,
            std::string{"vmaCreateImage: "} + std::to_string(static_cast<int>(vr))));
    }

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = raw_img;
    vci.viewType = info.view_type;
    vci.format = info.format;
    vci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    vci.subresourceRange.aspectMask = info.aspect;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = info.mip_levels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = info.array_layers;

    VkImageView view = VK_NULL_HANDLE;
    if (auto vr = vkCreateImageView(allocator.device(), &vci, nullptr, &view); vr != VK_SUCCESS) {
        vmaDestroyImage(allocator.handle(), raw_img, alloc);
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateImageView: "} + std::to_string(static_cast<int>(vr))));
    }

    Image img;
    img.owner_ = allocator.handle();
    img.device_ = allocator.device();
    img.handle_ = raw_img;
    img.view_ = view;
    img.allocation_ = alloc;
    img.format_ = info.format;
    img.extent_ = info.extent;
    return img;
}

Image::Image(Image&& other) noexcept
    : owner_(other.owner_),
      device_(other.device_),
      handle_(other.handle_),
      view_(other.view_),
      allocation_(other.allocation_),
      format_(other.format_),
      extent_(other.extent_) {
    other.owner_ = nullptr;
    other.device_ = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
    other.view_ = VK_NULL_HANDLE;
    other.allocation_ = nullptr;
    other.format_ = VK_FORMAT_UNDEFINED;
    other.extent_ = {0, 0, 0};
}

auto Image::operator=(Image&& other) noexcept -> Image& {
    if (this != &other) {
        destroy();
        owner_ = other.owner_;
        device_ = other.device_;
        handle_ = other.handle_;
        view_ = other.view_;
        allocation_ = other.allocation_;
        format_ = other.format_;
        extent_ = other.extent_;
        other.owner_ = nullptr;
        other.device_ = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
        other.view_ = VK_NULL_HANDLE;
        other.allocation_ = nullptr;
        other.format_ = VK_FORMAT_UNDEFINED;
        other.extent_ = {0, 0, 0};
    }
    return *this;
}

Image::~Image() {
    destroy();
}

void Image::destroy() noexcept {
    if (view_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, view_, nullptr);
        view_ = VK_NULL_HANDLE;
    }
    if (owner_ != nullptr && handle_ != VK_NULL_HANDLE) {
        vmaDestroyImage(owner_, handle_, allocation_);
    }
    owner_ = nullptr;
    device_ = VK_NULL_HANDLE;
    handle_ = VK_NULL_HANDLE;
    allocation_ = nullptr;
    format_ = VK_FORMAT_UNDEFINED;
    extent_ = {0, 0, 0};
}

}  // namespace noted::gpu
