#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

struct DescriptorBinding {
    std::uint32_t       binding         = 0;
    VkDescriptorType    type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    std::uint32_t       count           = 1;
    VkShaderStageFlags  stages          = VK_SHADER_STAGE_ALL_GRAPHICS;
    const VkSampler*    immutable_samplers = nullptr;
};

// VkDescriptorSetLayout RAII. Holds a copy of the binding array so debug
// tooling (and PipelineLayout::create) can introspect what the set looks
// like without keeping the original span alive.
class DescriptorSetLayout {
public:
    [[nodiscard]] static auto create(
        const Device&                  device,
        std::span<const DescriptorBinding> bindings) -> Result<DescriptorSetLayout>;

    DescriptorSetLayout(DescriptorSetLayout&& other) noexcept;
    auto operator=(DescriptorSetLayout&& other) noexcept -> DescriptorSetLayout&;
    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    auto operator=(const DescriptorSetLayout&) -> DescriptorSetLayout& = delete;
    ~DescriptorSetLayout();

    [[nodiscard]] auto handle() const noexcept -> VkDescriptorSetLayout { return handle_; }
    [[nodiscard]] auto bindings() const noexcept -> const std::vector<DescriptorBinding>& {
        return bindings_;
    }

private:
    DescriptorSetLayout() = default;
    void destroy() noexcept;

    VkDevice                       owner_   = VK_NULL_HANDLE;
    VkDescriptorSetLayout          handle_  = VK_NULL_HANDLE;
    std::vector<DescriptorBinding> bindings_;
};

}  // namespace noted::gpu
