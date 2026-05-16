#pragma once

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

struct SamplerCreateInfo {
    VkFilter mag_filter = VK_FILTER_LINEAR;
    VkFilter min_filter = VK_FILTER_LINEAR;
    VkSamplerMipmapMode mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerAddressMode address_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode address_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSamplerAddressMode address_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    float mip_lod_bias = 0.0F;
    bool anisotropy_enabled = false;
    float max_anisotropy = 1.0F;
    float min_lod = 0.0F;
    float max_lod = VK_LOD_CLAMP_NONE;
    VkBorderColor border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
};

class Sampler {
public:
    [[nodiscard]] static auto create(const Device& device,
                                     const SamplerCreateInfo& info = {}) -> Result<Sampler>;

    // Common presets.
    [[nodiscard]] static auto linear_clamp(const Device& d) -> Result<Sampler> {
        return create(d, SamplerCreateInfo{});
    }
    [[nodiscard]] static auto nearest_clamp(const Device& d) -> Result<Sampler> {
        return create(d,
                      SamplerCreateInfo{
                          .mag_filter = VK_FILTER_NEAREST,
                          .min_filter = VK_FILTER_NEAREST,
                          .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                      });
    }
    [[nodiscard]] static auto linear_repeat(const Device& d) -> Result<Sampler> {
        return create(d,
                      SamplerCreateInfo{
                          .address_u = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                          .address_v = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                          .address_w = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                      });
    }

    Sampler(Sampler&& other) noexcept;
    auto operator=(Sampler&& other) noexcept -> Sampler&;
    Sampler(const Sampler&) = delete;
    auto operator=(const Sampler&) -> Sampler& = delete;
    ~Sampler();

    [[nodiscard]] auto handle() const noexcept -> VkSampler { return handle_; }

private:
    Sampler() = default;
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkSampler handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
