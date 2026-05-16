#pragma once

#include <cstdint>
#include <span>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/descriptor_set_layout.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

struct DescriptorPoolSize {
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    std::uint32_t count = 1;
};

struct DescriptorPoolCreateInfo {
    std::uint32_t max_sets = 1;
    std::span<const DescriptorPoolSize> pool_sizes = {};
    // When true, sets the FREE_DESCRIPTOR_SET_BIT flag, allowing
    // vkFreeDescriptorSets. Off by default: callers reset the whole pool
    // for the standard frame-recycling pattern.
    bool allow_free = false;
    // Required when allocating from layouts that use
    // VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT.
    bool update_after_bind = false;
};

// VkDescriptorPool RAII. allocate(layout) returns a raw VkDescriptorSet
// whose lifetime is owned by the pool — destroy the pool to free all
// sets, or call reset() to recycle.
class DescriptorPool {
public:
    [[nodiscard]] static auto create(const Device& device, const DescriptorPoolCreateInfo& info)
        -> Result<DescriptorPool>;

    DescriptorPool(DescriptorPool&& other) noexcept;
    auto operator=(DescriptorPool&& other) noexcept -> DescriptorPool&;
    DescriptorPool(const DescriptorPool&) = delete;
    auto operator=(const DescriptorPool&) -> DescriptorPool& = delete;
    ~DescriptorPool();

    [[nodiscard]] auto handle() const noexcept -> VkDescriptorPool { return handle_; }
    [[nodiscard]] auto device() const noexcept -> VkDevice { return owner_; }

    // Allocate a single VkDescriptorSet using the given layout. The set's
    // lifetime is bound to this pool.
    [[nodiscard]] auto allocate(const DescriptorSetLayout& layout) -> Result<VkDescriptorSet>;

    void reset(VkDescriptorPoolResetFlags flags = 0) noexcept;

private:
    DescriptorPool() = default;
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkDescriptorPool handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
