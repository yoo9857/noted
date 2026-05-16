#include "noted/compositor/layer_compositor.hpp"

#include <algorithm>
#include <utility>

#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/profile.hpp"

namespace noted::compositor {

namespace {

// ---- Push-constant payload ------------------------------------------------
// Matches LayerPush in shaders/layer.slang (vec4 color). 16 bytes total.
struct LayerPush {
    float color[4];
};
static_assert(sizeof(LayerPush) == 16, "LayerPush must be 16 bytes");

// All channels write enabled. Same for every blend mode we support.
constexpr VkColorComponentFlags kAllChannels = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

[[nodiscard]] auto make_blend(VkBlendFactor src_color,
                              VkBlendFactor dst_color,
                              VkBlendOp color_op,
                              VkBlendFactor src_alpha,
                              VkBlendFactor dst_alpha,
                              VkBlendOp alpha_op) noexcept -> VkPipelineColorBlendAttachmentState {
    VkPipelineColorBlendAttachmentState s{};
    s.blendEnable = VK_TRUE;
    s.srcColorBlendFactor = src_color;
    s.dstColorBlendFactor = dst_color;
    s.colorBlendOp = color_op;
    s.srcAlphaBlendFactor = src_alpha;
    s.dstAlphaBlendFactor = dst_alpha;
    s.alphaBlendOp = alpha_op;
    s.colorWriteMask = kAllChannels;
    return s;
}

}  // namespace

auto blend_state_for(noted::domain::BlendMode mode,
                     bool& supported_out) noexcept -> VkPipelineColorBlendAttachmentState {
    using BM = noted::domain::BlendMode;
    supported_out = true;
    switch (mode) {
        case BM::normal:
            // out = src.rgb + dst.rgb * (1 - src.a). Standard "over".
            return make_blend(VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD,
                              VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD);
        case BM::screen:
            // out = src + dst * (1 - src). Symmetric inverted-multiply.
            return make_blend(VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                              VK_BLEND_OP_ADD,
                              VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD);
        case BM::linear_dodge:
            // out = src + dst. Additive.
            return make_blend(VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE,
                              VK_BLEND_OP_ADD,
                              VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD);
        case BM::multiply:
            // out = src * dst. FF can't honor src.a interpolation here,
            // so this mode assumes the layer is fully opaque (a=1). For
            // semi-transparent multiply layers a shader pass is needed
            // (deferred to feat/layer-compositor-shader).
            return make_blend(VK_BLEND_FACTOR_DST_COLOR,
                              VK_BLEND_FACTOR_ZERO,
                              VK_BLEND_OP_ADD,
                              VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD);

        case BM::overlay:
        case BM::soft_light:
        case BM::hard_light:
        case BM::color_dodge:
        case BM::color_burn:
        case BM::linear_burn:
        case BM::difference:
        case BM::exclusion:
        case BM::hue:
        case BM::saturation:
        case BM::color:
        case BM::luminosity:
            // Not expressible via fixed-function blend. Fall back to
            // NORMAL — the compositor logs this as a "fallback".
            supported_out = false;
            return make_blend(VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD,
                              VK_BLEND_FACTOR_ONE,
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                              VK_BLEND_OP_ADD);
    }
    // Unreachable for well-formed BlendMode values.
    supported_out = false;
    return make_blend(VK_BLEND_FACTOR_ONE,
                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                      VK_BLEND_OP_ADD,
                      VK_BLEND_FACTOR_ONE,
                      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                      VK_BLEND_OP_ADD);
}

auto resolve_composition(const noted::domain::LayerGraph& graph,
                         const LayerPayloadStore& store) -> Result<std::vector<CompositorEntry>> {
    auto order = graph.topological_order();
    if (!order) {
        return std::unexpected(std::move(order).error());
    }
    std::vector<CompositorEntry> out;
    out.reserve(order->size());
    for (const auto id : *order) {
        const auto* node = graph.find(id);
        if (node == nullptr || !node->visible) {
            continue;
        }
        const auto* payload = store.find(id);
        if (payload == nullptr) {
            continue;  // structural node (group, mask, etc.) — no draw of its own
        }
        out.push_back(CompositorEntry{
            .id = id,
            .blend = node->blend,
            .opacity = node->opacity,
            .payload = payload,
        });
    }
    return out;
}

auto LayerCompositor::slot_for(noted::domain::BlendMode mode,
                               bool& supported_out) noexcept -> Slot {
    using BM = noted::domain::BlendMode;
    supported_out = true;
    switch (mode) {
        case BM::normal:
            return Slot::normal;
        case BM::screen:
            return Slot::screen;
        case BM::linear_dodge:
            return Slot::linear_dodge;
        case BM::multiply:
            return Slot::multiply;
        default:
            supported_out = false;
            return Slot::normal;
    }
}

auto LayerCompositor::create(const CreateInfo& info) -> Result<LayerCompositor> {
    if (info.device == nullptr || info.vs_module == nullptr || info.ps_module == nullptr) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "LayerCompositor::create: device or shader module is null"));
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(LayerPush);

    auto layout = noted::gpu::PipelineLayout::create(
        *info.device,
        std::span<const noted::gpu::DescriptorSetLayout* const>{},
        std::span<const VkPushConstantRange>{&push_range, 1});
    if (!layout) {
        return std::unexpected(std::move(layout).error());
    }

    LayerCompositor c;
    c.layout_.emplace(std::move(*layout));

    // One pipeline per Slot. All share the same shaders; only blend
    // state differs. Build in the slot order pinned by slot_for().
    using BM = noted::domain::BlendMode;
    constexpr std::array<BM, static_cast<std::size_t>(Slot::count)> kModes{
        BM::normal,
        BM::screen,
        BM::linear_dodge,
        BM::multiply,
    };
    for (std::size_t i = 0; i < kModes.size(); ++i) {
        bool supported = true;
        const auto blend = blend_state_for(kModes[i], supported);
        auto p = noted::gpu::GraphicsPipelineBuilder{}
                     .add_stage(VK_SHADER_STAGE_VERTEX_BIT, *info.vs_module, "main")
                     .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *info.ps_module, "main")
                     .rasterization(
                         VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
                     .color_blend_attachment(blend)
                     .color_format(info.canvas_format)
                     .build(*info.device, *c.layout_);
        if (!p) {
            return std::unexpected(std::move(p).error());
        }
        c.pipelines_[i].emplace(std::move(*p));
    }
    return c;
}

void LayerCompositor::composite(VkCommandBuffer cb,
                                VkExtent2D canvas_extent,
                                const noted::domain::LayerGraph& graph,
                                const LayerPayloadStore& store) noexcept {
    NOTED_PROFILE_ZONE_N("LayerCompositor::composite");
    (void) canvas_extent;  // shader writes full NDC; viewport is dynamic

    auto resolved = resolve_composition(graph, store);
    if (!resolved) {
        // Cycle or dangling reference — silently skip the frame; the
        // caller saw the same error when it called validate() (and is
        // expected to call validate periodically). Compositor stays
        // silent so a malformed frame does not crash mid-record.
        ++fallback_count_;
        return;
    }
    if (resolved->empty() || !layout_.has_value()) {
        return;
    }

    const VkPipelineLayout layout_h = layout_->handle();
    constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipeline last_pipeline = VK_NULL_HANDLE;
    for (const auto& e : *resolved) {
        bool supported = true;
        const auto slot = slot_for(e.blend, supported);
        if (!supported) {
            ++fallback_count_;
        }
        const auto idx = static_cast<std::size_t>(slot);
        if (!pipelines_[idx].has_value()) {
            continue;  // shouldn't happen after create()
        }
        const VkPipeline pipeline_h = pipelines_[idx]->handle();
        if (pipeline_h != last_pipeline) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_h);
            last_pipeline = pipeline_h;
        }

        // Visit the payload variant. For SolidColor: multiply RGBA by
        // opacity to produce a pre-multiplied color the FF blend expects.
        LayerPush p{};
        std::visit(
            [&](const auto& payload) {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, SolidColor>) {
                    const float a = std::clamp(payload.a * e.opacity, 0.0F, 1.0F);
                    p.color[0] = payload.r * a;
                    p.color[1] = payload.g * a;
                    p.color[2] = payload.b * a;
                    p.color[3] = a;
                }
            },
            *e.payload);

        vkCmdPushConstants(cb, layout_h, kStages, 0, sizeof(LayerPush), &p);
        vkCmdDraw(cb, /*vertexCount=*/3, /*instanceCount=*/1, 0, 0);
    }
}

}  // namespace noted::compositor
