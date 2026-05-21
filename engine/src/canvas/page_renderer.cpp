#include "noted/engine/canvas/page_renderer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <utility>

#include "noted/engine/gpu/descriptor_set_layout.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/profile.hpp"

namespace noted::canvas {

namespace {

// Matches the `PageBgPush` struct in shaders/page_bg.slang. Layout is
// std430-equivalent; static_asserts pin the offsets so a future field
// reorder can't silently break the binding.
struct PageBgPush {
    float canvas_size[2];   // offset  0
    float origin_px[2];     // offset  8
    float extent_px[2];     // offset 16
    std::uint32_t bg_kind;  // offset 24
    std::uint32_t _pad;     // offset 28 — pads to 32
};
static_assert(sizeof(PageBgPush) == 32, "PageBgPush must be 32 bytes — check page_bg.slang");
static_assert(offsetof(PageBgPush, canvas_size) == 0);
static_assert(offsetof(PageBgPush, origin_px) == 8);
static_assert(offsetof(PageBgPush, extent_px) == 16);
static_assert(offsetof(PageBgPush, bg_kind) == 24);

}  // namespace

auto PageRenderer::create(const CreateInfo& info) -> Result<PageRenderer> {
    if (info.device == nullptr || info.vs_module == nullptr || info.ps_module == nullptr) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "PageRenderer::create: device / shader modules must be non-null"));
    }

    // Pipeline layout: no descriptor sets, one 32-byte push range
    // covering both stages. Vertex stage reads canvas_size /
    // origin / extent for the quad placement; fragment stage reads
    // bg_kind to select the pattern (lined / grid / dotted /
    // blank).
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PageBgPush);

    auto layout = noted::gpu::PipelineLayout::create(
        *info.device,
        std::span<const noted::gpu::DescriptorSetLayout* const>{},
        std::span<const VkPushConstantRange>{&push_range, 1});
    if (!layout) {
        return std::unexpected(std::move(layout).error());
    }

    // Pipeline: triangle list (the vertex shader builds a quad from
    // gl_VertexIndex), no vertex input, no culling. Standard
    // straight-alpha source-over blend so the fragment shader's
    // shadow apron composites against the desk-grey canvas clear.
    // The page body itself writes full alpha (= overwrite); only the
    // shadow apron pixels carry partial alpha. See `page_bg.slang`
    // and ADR 0033 for the visual model.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    auto pipeline =
        noted::gpu::GraphicsPipelineBuilder{}
            .add_stage(VK_SHADER_STAGE_VERTEX_BIT, *info.vs_module, "main")
            .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *info.ps_module, "main")
            .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .color_blend_attachment(blend)
            .color_format(info.canvas_format)
            .build(*info.device, *layout);
    if (!pipeline) {
        return std::unexpected(std::move(pipeline).error());
    }

    PageRenderer r;
    r.layout_.emplace(std::move(*layout));
    r.pipeline_.emplace(std::move(*pipeline));
    return r;
}

void PageRenderer::render(VkCommandBuffer cb,
                          VkExtent2D canvas_extent,
                          const PageList& list) noexcept {
    NOTED_PROFILE_ZONE_N("PageRenderer::render");
    if (!pipeline_.has_value() || !layout_.has_value() || list.empty()) {
        return;
    }

    const float cw = static_cast<float>(canvas_extent.width == 0 ? 1U : canvas_extent.width);
    const float ch = static_cast<float>(canvas_extent.height == 0 ? 1U : canvas_extent.height);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    const VkPipelineLayout layout_h = layout_->handle();
    constexpr VkShaderStageFlags kStages =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (const auto& page : list.pages()) {
        PageBgPush push{};
        push.canvas_size[0] = cw;
        push.canvas_size[1] = ch;
        push.origin_px[0] = page.origin_x_px;
        push.origin_px[1] = page.origin_y_px;
        push.extent_px[0] = page.extent_w_px;
        push.extent_px[1] = page.extent_h_px;
        push.bg_kind = static_cast<std::uint32_t>(page.background);
        push._pad = 0U;

        vkCmdPushConstants(cb, layout_h, kStages, 0, sizeof(PageBgPush), &push);
        vkCmdDraw(cb, /*vertexCount=*/6, /*instanceCount=*/1, 0, 0);
    }
}

}  // namespace noted::canvas
