#include "noted/engine/gpu/pipeline_layout.hpp"

#include <string>
#include <vector>

namespace noted::gpu {

auto PipelineLayout::create(const Device& device,
                            std::span<const DescriptorSetLayout* const> set_layouts,
                            std::span<const VkPushConstantRange> push_ranges)
    -> Result<PipelineLayout> {
    std::vector<VkDescriptorSetLayout> raw_layouts;
    raw_layouts.reserve(set_layouts.size());
    for (const auto* sl : set_layouts) {
        raw_layouts.push_back(sl != nullptr ? sl->handle() : VK_NULL_HANDLE);
    }

    VkPipelineLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = static_cast<std::uint32_t>(raw_layouts.size());
    ci.pSetLayouts = raw_layouts.empty() ? nullptr : raw_layouts.data();
    ci.pushConstantRangeCount = static_cast<std::uint32_t>(push_ranges.size());
    ci.pPushConstantRanges = push_ranges.empty() ? nullptr : push_ranges.data();

    VkPipelineLayout raw = VK_NULL_HANDLE;
    if (auto vr = vkCreatePipelineLayout(device.handle(), &ci, nullptr, &raw); vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_validation_failed,
            std::string{"vkCreatePipelineLayout: "} + std::to_string(static_cast<int>(vr))));
    }
    PipelineLayout out;
    out.owner_ = device.handle();
    out.handle_ = raw;
    return out;
}

PipelineLayout::PipelineLayout(PipelineLayout&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_ = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto PipelineLayout::operator=(PipelineLayout&& other) noexcept -> PipelineLayout& {
    if (this != &other) {
        destroy();
        owner_ = other.owner_;
        handle_ = other.handle_;
        other.owner_ = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

PipelineLayout::~PipelineLayout() {
    destroy();
}

void PipelineLayout::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_ = VK_NULL_HANDLE;
}

}  // namespace noted::gpu
