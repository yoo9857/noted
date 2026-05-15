#include "noted/engine/gpu/sampler.hpp"

#include <string>

namespace noted::gpu {

auto Sampler::create(const Device& device, const SamplerCreateInfo& info) -> Result<Sampler> {
    VkSamplerCreateInfo ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ci.magFilter        = info.mag_filter;
    ci.minFilter        = info.min_filter;
    ci.mipmapMode       = info.mipmap_mode;
    ci.addressModeU     = info.address_u;
    ci.addressModeV     = info.address_v;
    ci.addressModeW     = info.address_w;
    ci.mipLodBias       = info.mip_lod_bias;
    ci.anisotropyEnable = info.anisotropy_enabled ? VK_TRUE : VK_FALSE;
    ci.maxAnisotropy    = info.max_anisotropy;
    ci.compareEnable    = VK_FALSE;
    ci.compareOp        = VK_COMPARE_OP_ALWAYS;
    ci.minLod           = info.min_lod;
    ci.maxLod           = info.max_lod;
    ci.borderColor      = info.border_color;
    ci.unnormalizedCoordinates = VK_FALSE;

    VkSampler raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateSampler(device.handle(), &ci, nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateSampler: "} + std::to_string(static_cast<int>(vr))));
    }
    Sampler s;
    s.owner_  = device.handle();
    s.handle_ = raw;
    return s;
}

Sampler::Sampler(Sampler&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_  = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto Sampler::operator=(Sampler&& other) noexcept -> Sampler& {
    if (this != &other) {
        destroy();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

Sampler::~Sampler() { destroy(); }

void Sampler::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroySampler(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_  = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
