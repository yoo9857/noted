#include "noted/engine/gpu/graphics_pipeline.hpp"

#include <array>
#include <cstring>
#include <string>
#include <utility>

namespace noted::gpu {

// ---- GraphicsPipeline RAII ----------------------------------------------

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : owner_(other.owner_), handle_(other.handle_) {
    other.owner_ = VK_NULL_HANDLE;
    other.handle_ = VK_NULL_HANDLE;
}

auto GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept -> GraphicsPipeline& {
    if (this != &other) {
        destroy();
        owner_ = other.owner_;
        handle_ = other.handle_;
        other.owner_ = VK_NULL_HANDLE;
        other.handle_ = VK_NULL_HANDLE;
    }
    return *this;
}

GraphicsPipeline::~GraphicsPipeline() {
    destroy();
}

void GraphicsPipeline::destroy() noexcept {
    if (handle_ != VK_NULL_HANDLE && owner_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(owner_, handle_, nullptr);
    }
    handle_ = VK_NULL_HANDLE;
    owner_ = VK_NULL_HANDLE;
}

// ---- Builder ------------------------------------------------------------

GraphicsPipelineBuilder::GraphicsPipelineBuilder() {
    color_blend_.blendEnable = VK_FALSE;
    color_blend_.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_.alphaBlendOp = VK_BLEND_OP_ADD;

    // Sensible defaults: viewport + scissor are dynamic so the pipeline
    // survives window resize without a rebuild.
    dynamic_.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    dynamic_.push_back(VK_DYNAMIC_STATE_SCISSOR);
}

auto GraphicsPipelineBuilder::add_stage(VkShaderStageFlagBits stage,
                                        const ShaderModule& module,
                                        const char* entry) -> GraphicsPipelineBuilder& {
    stages_.push_back(Stage{
        .stage = stage,
        .module = module.handle(),
        .entry = entry != nullptr ? std::string{entry} : std::string{"main"},
    });
    return *this;
}

auto GraphicsPipelineBuilder::vertex_input(std::span<const VertexInputBinding> bindings,
                                           std::span<const VertexInputAttribute> attributes)
    -> GraphicsPipelineBuilder& {
    vertex_bindings_.assign(bindings.begin(), bindings.end());
    vertex_attributes_.assign(attributes.begin(), attributes.end());
    return *this;
}

auto GraphicsPipelineBuilder::topology(VkPrimitiveTopology t) -> GraphicsPipelineBuilder& {
    topology_ = t;
    return *this;
}

auto GraphicsPipelineBuilder::rasterization(VkPolygonMode polygon,
                                            VkCullModeFlags cull,
                                            VkFrontFace front) -> GraphicsPipelineBuilder& {
    polygon_ = polygon;
    cull_ = cull;
    front_ = front;
    return *this;
}

auto GraphicsPipelineBuilder::multisample(VkSampleCountFlagBits samples)
    -> GraphicsPipelineBuilder& {
    samples_ = samples;
    return *this;
}

auto GraphicsPipelineBuilder::depth_test(bool enable, VkCompareOp op) -> GraphicsPipelineBuilder& {
    depth_test_ = enable;
    depth_op_ = op;
    return *this;
}

auto GraphicsPipelineBuilder::depth_write(bool enable) -> GraphicsPipelineBuilder& {
    depth_write_ = enable;
    return *this;
}

auto GraphicsPipelineBuilder::color_blend_attachment(const VkPipelineColorBlendAttachmentState& s)
    -> GraphicsPipelineBuilder& {
    color_blend_ = s;
    return *this;
}

auto GraphicsPipelineBuilder::color_format(VkFormat f) -> GraphicsPipelineBuilder& {
    color_fmt_ = f;
    return *this;
}

auto GraphicsPipelineBuilder::depth_format(VkFormat f) -> GraphicsPipelineBuilder& {
    depth_fmt_ = f;
    return *this;
}

auto GraphicsPipelineBuilder::stencil_format(VkFormat f) -> GraphicsPipelineBuilder& {
    stencil_fmt_ = f;
    return *this;
}

auto GraphicsPipelineBuilder::dynamic_states(std::span<const VkDynamicState> states)
    -> GraphicsPipelineBuilder& {
    dynamic_.assign(states.begin(), states.end());
    return *this;
}

auto GraphicsPipelineBuilder::build(const Device& device,
                                    const PipelineLayout& layout) -> Result<GraphicsPipeline> {
    if (stages_.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "GraphicsPipelineBuilder::build: no shader stages added"));
    }

    std::vector<VkPipelineShaderStageCreateInfo> stage_cis;
    stage_cis.reserve(stages_.size());
    for (const auto& s : stages_) {
        VkPipelineShaderStageCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        sci.stage = s.stage;
        sci.module = s.module;
        sci.pName = s.entry.c_str();
        stage_cis.push_back(sci);
    }

    std::vector<VkVertexInputBindingDescription> vk_bindings;
    vk_bindings.reserve(vertex_bindings_.size());
    for (const auto& b : vertex_bindings_) {
        vk_bindings.push_back(VkVertexInputBindingDescription{
            .binding = b.binding,
            .stride = b.stride,
            .inputRate = b.rate,
        });
    }
    std::vector<VkVertexInputAttributeDescription> vk_attrs;
    vk_attrs.reserve(vertex_attributes_.size());
    for (const auto& a : vertex_attributes_) {
        vk_attrs.push_back(VkVertexInputAttributeDescription{
            .location = a.location,
            .binding = a.binding,
            .format = a.format,
            .offset = a.offset,
        });
    }

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = static_cast<std::uint32_t>(vk_bindings.size());
    vi.pVertexBindingDescriptions = vk_bindings.empty() ? nullptr : vk_bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(vk_attrs.size());
    vi.pVertexAttributeDescriptions = vk_attrs.empty() ? nullptr : vk_attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = topology_;

    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1;
    vs.scissorCount = 1;
    // pViewports / pScissors are null because viewport + scissor are dynamic.

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = polygon_;
    rs.cullMode = cull_;
    rs.frontFace = front_;
    rs.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = samples_;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = depth_test_ ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = depth_write_ ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = depth_op_;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &color_blend_;

    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = static_cast<std::uint32_t>(dynamic_.size());
    dyn.pDynamicStates = dynamic_.empty() ? nullptr : dynamic_.data();

    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &color_fmt_;
    rendering.depthAttachmentFormat = depth_fmt_;
    rendering.stencilAttachmentFormat = stencil_fmt_;

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.pNext = &rendering;  // dynamic_rendering — no VkRenderPass
    gpci.stageCount = static_cast<std::uint32_t>(stage_cis.size());
    gpci.pStages = stage_cis.data();
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vs;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pDepthStencilState = &ds;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = layout.handle();
    gpci.renderPass = VK_NULL_HANDLE;  // dynamic_rendering
    gpci.subpass = 0;

    VkPipeline raw = VK_NULL_HANDLE;
    if (auto vr =
            vkCreateGraphicsPipelines(device.handle(), VK_NULL_HANDLE, 1, &gpci, nullptr, &raw);
        vr != VK_SUCCESS) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::gpu_shader_compile_failed,
            std::string{"vkCreateGraphicsPipelines: "} + std::to_string(static_cast<int>(vr))));
    }
    return GraphicsPipeline::adopt(device.handle(), raw);
}

}  // namespace noted::gpu
