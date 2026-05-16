#pragma once

#include <span>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/descriptor_set_layout.hpp"
#include "noted/engine/gpu/device.hpp"

namespace noted::gpu {

// VkPipelineLayout RAII.
//
// Takes:
//   - set_layouts: one entry per descriptor set the pipeline will bind.
//                  Order matches `layout(set = N)` in the shader.
//   - push_ranges: one entry per VK_PUSH_CONSTANT range.
class PipelineLayout {
public:
    [[nodiscard]] static auto create(const Device& device,
                                     std::span<const DescriptorSetLayout* const> set_layouts,
                                     std::span<const VkPushConstantRange> push_ranges = {})
        -> Result<PipelineLayout>;

    PipelineLayout(PipelineLayout&& other) noexcept;
    auto operator=(PipelineLayout&& other) noexcept -> PipelineLayout&;
    PipelineLayout(const PipelineLayout&) = delete;
    auto operator=(const PipelineLayout&) -> PipelineLayout& = delete;
    ~PipelineLayout();

    [[nodiscard]] auto handle() const noexcept -> VkPipelineLayout { return handle_; }

private:
    PipelineLayout() = default;
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkPipelineLayout handle_ = VK_NULL_HANDLE;
};

}  // namespace noted::gpu
