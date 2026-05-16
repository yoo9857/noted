#include "noted/engine/gpu/descriptor_set_layout.hpp"

#include <string>

namespace noted::gpu {

auto DescriptorSetLayout::create(const Device& device,
                                 const CreateInfo& info) -> Result<DescriptorSetLayout> {
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    std::vector<VkDescriptorBindingFlags> vk_flags;
    vk_bindings.reserve(info.bindings.size());
    vk_flags.reserve(info.bindings.size());

    bool any_per_binding_flags = false;
    for (const auto& b : info.bindings) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding = b.binding;
        lb.descriptorType = b.type;
        lb.descriptorCount = b.count;
        lb.stageFlags = b.stages;
        lb.pImmutableSamplers = b.immutable_samplers;
        vk_bindings.push_back(lb);
        vk_flags.push_back(b.binding_flags);
        if (b.binding_flags != 0) {
            any_per_binding_flags = true;
        }
    }

    VkDescriptorSetLayoutBindingFlagsCreateInfo bf{};
    bf.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bf.bindingCount = static_cast<std::uint32_t>(vk_flags.size());
    bf.pBindingFlags = vk_flags.empty() ? nullptr : vk_flags.data();

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<std::uint32_t>(vk_bindings.size());
    ci.pBindings = vk_bindings.empty() ? nullptr : vk_bindings.data();
    if (any_per_binding_flags) {
        ci.pNext = &bf;
    }
    if (info.update_after_bind) {
        ci.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    }

    VkDescriptorSetLayout raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateDescriptorSetLayout(device.handle(), &ci, nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateDescriptorSetLayout: "} + std::to_string(static_cast<int>(vr))));
    }
    DescriptorSetLayout out;
    out.owner_ = device.handle();
    out.handle_ = raw;
    out.bindings_.assign(info.bindings.begin(), info.bindings.end());
    return out;
}

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_), bindings_(std::move(other.bindings_)) {
    other.owner_ = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept -> DescriptorSetLayout& {
    if (this != &other) {
        destroy();
        owner_ = other.owner_;
        handle_ = other.handle_;
        bindings_ = std::move(other.bindings_);
        other.owner_ = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

DescriptorSetLayout::~DescriptorSetLayout() {
    destroy();
}

void DescriptorSetLayout::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_ = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
