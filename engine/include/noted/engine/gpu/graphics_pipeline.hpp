#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/gpu/shader_module.hpp"

namespace noted::gpu {

struct VertexInputAttribute {
    std::uint32_t location = 0;
    std::uint32_t binding = 0;
    VkFormat format = VK_FORMAT_R32G32B32_SFLOAT;
    std::uint32_t offset = 0;
};

struct VertexInputBinding {
    std::uint32_t binding = 0;
    std::uint32_t stride = 0;
    VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX;
};

// Move-only RAII over VkPipeline (graphics bind point).
class GraphicsPipeline {
public:
    GraphicsPipeline() = default;
    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    auto operator=(GraphicsPipeline&& other) noexcept -> GraphicsPipeline&;
    GraphicsPipeline(const GraphicsPipeline&) = delete;
    auto operator=(const GraphicsPipeline&) -> GraphicsPipeline& = delete;
    ~GraphicsPipeline();

    [[nodiscard]] auto handle() const noexcept -> VkPipeline { return handle_; }

    // Used by the builder. Not part of the public surface.
    static auto adopt(VkDevice device, VkPipeline raw) noexcept -> GraphicsPipeline {
        GraphicsPipeline p;
        p.owner_ = device;
        p.handle_ = raw;
        return p;
    }

private:
    void destroy() noexcept;

    VkDevice owner_ = VK_NULL_HANDLE;
    VkPipeline handle_ = VK_NULL_HANDLE;
};

// Fluent builder for graphics pipelines.
//
// Defaults are tuned for the most common renderer use case:
//   - triangle list, counter-clockwise front face, back-face culling
//   - depth: disabled (override with depth_test for 3D)
//   - one color attachment, no blending
//   - dynamic viewport + scissor (don't bake window size into the pipeline)
//   - dynamic_rendering: caller sets color_format() / depth_format() instead
//     of providing a VkRenderPass
class GraphicsPipelineBuilder {
public:
    GraphicsPipelineBuilder();

    auto add_stage(VkShaderStageFlagBits stage,
                   const ShaderModule& module,
                   const char* entry = "main") -> GraphicsPipelineBuilder&;

    auto vertex_input(std::span<const VertexInputBinding> bindings,
                      std::span<const VertexInputAttribute> attributes) -> GraphicsPipelineBuilder&;

    auto topology(VkPrimitiveTopology t) -> GraphicsPipelineBuilder&;

    auto rasterization(VkPolygonMode polygon = VK_POLYGON_MODE_FILL,
                       VkCullModeFlags cull = VK_CULL_MODE_BACK_BIT,
                       VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE)
        -> GraphicsPipelineBuilder&;

    auto multisample(VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT)
        -> GraphicsPipelineBuilder&;

    auto depth_test(bool enable,
                    VkCompareOp op = VK_COMPARE_OP_LESS_OR_EQUAL) -> GraphicsPipelineBuilder&;
    auto depth_write(bool enable) -> GraphicsPipelineBuilder&;

    // Replace the single default color attachment with the caller's blend
    // state. Most callers stay with the no-blend default and just call
    // color_format(...) to wire the swapchain's format.
    auto color_blend_attachment(const VkPipelineColorBlendAttachmentState& s)
        -> GraphicsPipelineBuilder&;

    // dynamic_rendering: declare attachment formats here instead of a
    // VkRenderPass.
    auto color_format(VkFormat f) -> GraphicsPipelineBuilder&;
    auto depth_format(VkFormat f) -> GraphicsPipelineBuilder&;
    auto stencil_format(VkFormat f) -> GraphicsPipelineBuilder&;

    auto dynamic_states(std::span<const VkDynamicState> states) -> GraphicsPipelineBuilder&;

    [[nodiscard]] auto build(const Device& device,
                             const PipelineLayout& layout) -> Result<GraphicsPipeline>;

private:
    struct Stage {
        VkShaderStageFlagBits stage{};
        VkShaderModule module{};
        std::string entry;
    };

    std::vector<Stage> stages_;
    std::vector<VertexInputBinding> vertex_bindings_;
    std::vector<VertexInputAttribute> vertex_attributes_;
    VkPrimitiveTopology topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPolygonMode polygon_ = VK_POLYGON_MODE_FILL;
    VkCullModeFlags cull_ = VK_CULL_MODE_BACK_BIT;
    VkFrontFace front_ = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkSampleCountFlagBits samples_ = VK_SAMPLE_COUNT_1_BIT;
    bool depth_test_ = false;
    bool depth_write_ = false;
    VkCompareOp depth_op_ = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState color_blend_{};
    VkFormat color_fmt_ = VK_FORMAT_B8G8R8A8_SRGB;
    VkFormat depth_fmt_ = VK_FORMAT_UNDEFINED;
    VkFormat stencil_fmt_ = VK_FORMAT_UNDEFINED;
    std::vector<VkDynamicState> dynamic_;
};

}  // namespace noted::gpu
