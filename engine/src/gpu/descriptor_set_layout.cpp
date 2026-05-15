#include "noted/engine/gpu/descriptor_set_layout.hpp"

#include <string>

namespace noted::gpu {

auto DescriptorSetLayout::create(
    const Device&                          device,
    std::span<const DescriptorBinding>     bindings) -> Result<DescriptorSetLayout> {
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(bindings.size());
    for (const auto& b : bindings) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding            = b.binding;
        lb.descriptorType     = b.type;
        lb.descriptorCount    = b.count;
        lb.stageFlags         = b.stages;
        lb.pImmutableSamplers = b.immutable_samplers;
        vk_bindings.push_back(lb);
    }

    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<std::uint32_t>(vk_bindings.size());
    ci.pBindings    = vk_bindings.empty() ? nullptr : vk_bindings.data();

    VkDescriptorSetLayout raw = VK_NULL_HANDLE;
    if (auto vr = vkCreateDescriptorSetLayout(device.handle(), &ci, nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreateDescriptorSetLayout: "} +
                std::to_string(static_cast<int>(vr))));
    }
    DescriptorSetLayout out;
    out.owner_    = device.handle();
    out.handle_   = raw;
    out.bindings_.assign(bindings.begin(), bindings.end());
    return out;
}

DescriptorSetLayout::DescriptorSetLayout(DescriptorSetLayout&& other) noexcept
    : owner_(other.owner_),
      handle_(other.handle_),
      bindings_(std::move(other.bindings_)) {
    other.owner_  = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto DescriptorSetLayout::operator=(DescriptorSetLayout&& other) noexcept
    -> DescriptorSetLayout& {
    if (this != &other) {
        destroy();
        owner_    = other.owner_;
        handle_   = other.handle_;
        bindings_ = std::move(other.bindings_);
        other.owner_  = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

DescriptorSetLayout::~DescriptorSetLayout() { destroy(); }

void DescriptorSetLayout::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_  = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
