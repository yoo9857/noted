#include "noted/engine/gpu/descriptor_pool.hpp"

#include <string>
#include <vector>

namespace noted::gpu {

auto DescriptorPool::create(const Device& device,
                            const DescriptorPoolCreateInfo& info)
    -> Result<DescriptorPool> {
    if (info.max_sets == 0) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "DescriptorPool::create: max_sets must be >= 1"));
    }

    std::vector<VkDescriptorPoolSize> sizes;
    sizes.reserve(info.pool_sizes.size());
    for (const auto& s : info.pool_sizes) {
        sizes.push_back(VkDescriptorPoolSize{
            .type            = s.type,
            .descriptorCount = s.count,
        });
    }

    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.flags         = info.allow_free
                        ? static_cast<VkDescriptorPoolCreateFlags>(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                        : 0;
    ci.maxSets       = info.max_sets;
    ci.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    ci.pPoolSizes    = sizes.empty() ? nullptr : sizes.data();

    VkDescriptorPool raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateDescriptorPool(device.handle(), &ci, nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateDescriptorPool: "} +
                std::to_string(static_cast<int>(vr))));
    }
    DescriptorPool p;
    p.owner_  = device.handle();
    p.handle_ = raw;
    return p;
}

DescriptorPool::DescriptorPool(DescriptorPool&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_  = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto DescriptorPool::operator=(DescriptorPool&& other) noexcept -> DescriptorPool& {
    if (this != &other) {
        destroy();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

DescriptorPool::~DescriptorPool() { destroy(); }

void DescriptorPool::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_  = VK_NULL_HANDLE;
}

auto DescriptorPool::allocate(const DescriptorSetLayout& layout)
    -> Result<VkDescriptorSet> {
    const VkDescriptorSetLayout layout_h = layout.handle();
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool     = handle_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts        = &layout_h;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (auto vr = vkAllocateDescriptorSets(owner_, &ai, &set); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_out_of_memory,
            std::string{"vkAllocateDescriptorSets: "} +
                std::to_string(static_cast<int>(vr))));
    }
    return set;
}

void DescriptorPool::reset(VkDescriptorPoolResetFlags flags) noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        (void)vkResetDescriptorPool(owner_, handle_, flags);
    }
}

}  // namespace noted::gpu
