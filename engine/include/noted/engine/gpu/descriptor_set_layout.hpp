#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

struct DescriptorBinding {
    std::uint32_t binding = 0;
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    std::uint32_t count = 1;
    VkShaderStageFlags stages = VK_SHADER_STAGE_ALL_GRAPHICS;
    const VkSampler* immutable_samplers = nullptr;
    // Per-binding flags (Vulkan 1.2 core: VkDescriptorBindingFlags).
    // Set this to VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
    // VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT to make the binding
    // bindless-friendly (descriptors can be updated while the set is in use
    // and not all slots have to be populated). Zero = no flags, the
    // pre-1.2 behavior.
    VkDescriptorBindingFlags binding_flags = 0;
};

// VkDescriptorSetLayout RAII. Holds a copy of the binding array so debug
// tooling (and PipelineLayout::create) can introspect what the set looks
// like without keeping the original span alive.
class DescriptorSetLayout {
public:
    struct CreateInfo {
        std::span<const DescriptorBinding> bindings;
        // When any binding uses UPDATE_AFTER_BIND_BIT, the pool the set is
        // allocated from must also enable UPDATE_AFTER_BIND_BIT, and the
        // layout itself needs the corresponding create flag. Setting
        // update_after_bind = true sets the layout flag; the binding flags
        // still come from each binding's `binding_flags` field.
        bool update_after_bind = false;
    };

    [[nodiscard]] static auto create(const Device& device,
                                     const CreateInfo& info) -> Result<DescriptorSetLayout>;

    // Convenience overload for the common simple case.
    [[nodiscard]] static auto create(const Device& device,
                                     std::span<const DescriptorBinding> bindings)
        -> Result<DescriptorSetLayout> {
        return create(device, CreateInfo{.bindings = bindings});
    }

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

    VkDevice owner_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout handle_ = VK_NULL_HANDLE;
    std::vector<DescriptorBinding> bindings_;
};

}  // namespace noted::gpu
