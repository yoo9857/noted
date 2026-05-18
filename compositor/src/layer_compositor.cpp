#include "noted/compositor/layer_compositor.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include "noted/engine/gpu/allocator.hpp"
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
    if (info.allocator == nullptr || info.device == nullptr || info.vs_module == nullptr ||
        info.ps_module == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "LayerCompositor::create: allocator, device, or shader module is null"));
    }

    // ---- Descriptor set (set=0, binding=0): R8 mask sampler. ---------------
    const std::array<noted::gpu::DescriptorBinding, 1> bindings{{{
        .binding = 0,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
        .stages = VK_SHADER_STAGE_FRAGMENT_BIT,
    }}};
    auto set_layout = noted::gpu::DescriptorSetLayout::create(*info.device, bindings);
    if (!set_layout) {
        return std::unexpected(std::move(set_layout).error());
    }

    const std::array<noted::gpu::DescriptorPoolSize, 1> pool_sizes{{{
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .count = 1,
    }}};
    auto pool = noted::gpu::DescriptorPool::create(*info.device,
                                                   noted::gpu::DescriptorPoolCreateInfo{
                                                       .max_sets = 1,
                                                       .pool_sizes = pool_sizes,
                                                   });
    if (!pool) {
        return std::unexpected(std::move(pool).error());
    }
    auto raw_set = pool->allocate(*set_layout);
    if (!raw_set) {
        return std::unexpected(std::move(raw_set).error());
    }
    noted::gpu::DescriptorSet mask_set{info.device->handle(), *raw_set};

    auto sampler = noted::gpu::Sampler::linear_clamp(*info.device);
    if (!sampler) {
        return std::unexpected(std::move(sampler).error());
    }

    // 1×1 dummy mask. Initial clear (to white = "all selected") is
    // deferred to the first composite() call so create() never needs a
    // command buffer / queue.
    auto dummy = noted::gpu::SelectionMask::create(*info.allocator,
                                                   noted::gpu::SelectionMaskCreateInfo{
                                                       .extent = VkExtent2D{1, 1},
                                                   });
    if (!dummy) {
        return std::unexpected(std::move(dummy).error());
    }

    // ---- Pipeline layout (one descriptor set + push range). ----------------
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(LayerPush);

    const std::array<const noted::gpu::DescriptorSetLayout*, 1> set_layouts{&*set_layout};
    auto pipeline_layout = noted::gpu::PipelineLayout::create(
        *info.device, set_layouts, std::span<const VkPushConstantRange>{&push_range, 1});
    if (!pipeline_layout) {
        return std::unexpected(std::move(pipeline_layout).error());
    }

    LayerCompositor c;
    c.set_layout_.emplace(std::move(*set_layout));
    c.descriptor_pool_.emplace(std::move(*pool));
    c.mask_set_ = mask_set;
    c.sampler_.emplace(std::move(*sampler));
    c.dummy_mask_.emplace(std::move(*dummy));
    c.pipeline_layout_.emplace(std::move(*pipeline_layout));

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
                     .build(*info.device, *c.pipeline_layout_);
        if (!p) {
            return std::unexpected(std::move(p).error());
        }
        c.pipelines_[i].emplace(std::move(*p));
    }
    return c;
}

void LayerCompositor::ensure_dummy_initialized_(VkCommandBuffer cb) noexcept {
    if (dummy_initialized_ || !dummy_mask_.has_value()) {
        return;
    }
    // Transition dummy → TRANSFER_DST, clear to 1.0 (all selected),
    // transition → SHADER_READ_ONLY. Single barrier-clear-barrier
    // sequence on the caller's command buffer.
    dummy_mask_->transition_to(cb,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_ACCESS_2_TRANSFER_WRITE_BIT,
                               VK_PIPELINE_STAGE_2_CLEAR_BIT);

    VkClearColorValue clear{};
    clear.float32[0] = 1.0F;
    clear.float32[1] = 0.0F;
    clear.float32[2] = 0.0F;
    clear.float32[3] = 0.0F;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(
        cb, dummy_mask_->handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    dummy_mask_->transition_to(cb,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    dummy_initialized_ = true;
}

void LayerCompositor::composite(VkCommandBuffer cb,
                                VkExtent2D canvas_extent,
                                const noted::domain::LayerGraph& graph,
                                const LayerPayloadStore& store,
                                const noted::gpu::SelectionMask* mask) noexcept {
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
    if (resolved->empty() || !pipeline_layout_.has_value() || !sampler_.has_value() ||
        !dummy_mask_.has_value()) {
        return;
    }

    // First-call: prepare the dummy mask. Cheap (one clear) and only
    // runs once over the compositor's lifetime.
    ensure_dummy_initialized_(cb);

    // Pick the mask to bind. The caller's mask must already be in
    // SHADER_READ_ONLY_OPTIMAL — SelectionRasterizer::record() handles
    // that. The dummy was just transitioned by ensure_dummy_initialized_.
    const VkImageView mask_view = (mask != nullptr) ? mask->view() : dummy_mask_->view();
    noted::gpu::DescriptorWriter{mask_set_}
        .write_combined_image_sampler(0, mask_view, sampler_->handle())
        .commit();

    const VkPipelineLayout layout_h = pipeline_layout_->handle();
    constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_FRAGMENT_BIT;

    const VkDescriptorSet set_h = mask_set_.handle();
    vkCmdBindDescriptorSets(
        cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_h, 0, 1, &set_h, 0, nullptr);

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
