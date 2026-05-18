#pragma once

// Layer compositor — walks a `domain::LayerGraph` in topological order
// and records draw commands that produce the document's final image
// in the canvas render target.
//
// MVP scope:
//   - Payload kinds supported: `SolidColor` (one fullscreen triangle
//     per layer, fixed-function blend).
//   - Blend modes supported via fixed-function blend: `normal`,
//     `screen`, `linear_dodge` (additive), and `multiply` (approximated
//     for opaque layers). The remaining 12 Photoshop modes fall back to
//     `normal` and the fallback is counted (`fallback_count()`) so
//     a future shader-based compositor can be benchmarked against the
//     FF path it replaces.
//   - One pipeline per supported mode, all sharing the same shader.
//     Pipeline selection at composite-time costs one indexed array
//     read.
//   - Selection masking: every fragment multiplies its output by an
//     R8 mask sample. When the caller passes no mask, an internal
//     1×1 "all selected" dummy is bound so the shader stays
//     unconditional. See ADR 0022.
//
// Public API stays GPU-format-agnostic on the consumer side: the host
// owns the canvas, hands us a command buffer + extent, and the
// compositor records the draws. Caller is responsible for the
// surrounding `vkCmdBeginRendering` / `vkCmdEndRendering` pair —
// the compositor never opens or closes a render pass itself.
//
// Rationale: see docs/architecture/0019-layer-compositor.md and
// docs/architecture/0022-compositor-masking.md.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/compositor/layer_payload.hpp"
#include "noted/domain/layer/layer.hpp"
#include "noted/engine/error/error.hpp"
#include "noted/engine/gpu/descriptor_pool.hpp"
#include "noted/engine/gpu/descriptor_set.hpp"
#include "noted/engine/gpu/descriptor_set_layout.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/gpu/sampler.hpp"
#include "noted/engine/gpu/selection_mask.hpp"

namespace noted::gpu {
class Allocator;
class Device;
class ShaderModule;
}  // namespace noted::gpu

namespace noted::compositor {

// Resolved per-layer information the compositor draws in order. Pure
// data — building this is testable without a GPU.
struct CompositorEntry {
    noted::domain::LayerId id;
    noted::domain::BlendMode blend;
    float opacity;                // [0, 1]
    const LayerPayload* payload;  // never null in the vector
};

// Pure function: pulls topological_order() out of the graph, drops
// invisible nodes and nodes without a registered payload, returns the
// ordered draw list. Errors with whatever the graph's
// `topological_order()` returns (cycles, dangling references).
[[nodiscard]] auto resolve_composition(const noted::domain::LayerGraph& graph,
                                       const LayerPayloadStore& store)
    -> Result<std::vector<CompositorEntry>>;

// Pure mapping `BlendMode → blend state`. `supported_out` is set to
// true for modes implemented via fixed-function; false for the modes
// that fall back to NORMAL (shader-based composite lands in a follow-up).
[[nodiscard]] auto blend_state_for(noted::domain::BlendMode mode, bool& supported_out) noexcept
    -> VkPipelineColorBlendAttachmentState;

class LayerCompositor {
public:
    struct CreateInfo {
        const noted::gpu::Allocator* allocator;  // for the dummy mask
        const noted::gpu::Device* device;
        const noted::gpu::ShaderModule* vs_module;
        const noted::gpu::ShaderModule* ps_module;
        VkFormat canvas_format;
    };

    [[nodiscard]] static auto create(const CreateInfo& info) -> Result<LayerCompositor>;

    LayerCompositor(LayerCompositor&&) noexcept = default;
    auto operator=(LayerCompositor&&) noexcept -> LayerCompositor& = default;
    LayerCompositor(const LayerCompositor&) = delete;
    auto operator=(const LayerCompositor&) -> LayerCompositor& = delete;
    ~LayerCompositor() = default;

    // Records one draw per visible / payload-bearing layer in topological
    // order. Caller is responsible for an active vkCmdBeginRendering
    // whose color attachment is the canvas image (COLOR_ATTACHMENT_OPTIMAL).
    //
    // `mask`: optional R8 selection mask. The fragment shader multiplies
    // each layer's output by `mask.r` at the matching canvas-UV. Pass
    // nullptr for unmasked rendering — the compositor binds an internal
    // 1×1 "all selected" dummy so the shader stays unconditional.
    //
    // When `mask` is non-null, the caller is responsible for having
    // transitioned it to SHADER_READ_ONLY_OPTIMAL before this call
    // (`SelectionRasterizer::record()` does this automatically).
    //
    // Failures (cycle / dangling input) increment the fallback counter
    // and produce no draws.
    void composite(VkCommandBuffer cb,
                   VkExtent2D canvas_extent,
                   const noted::domain::LayerGraph& graph,
                   const LayerPayloadStore& store,
                   const noted::gpu::SelectionMask* mask = nullptr) noexcept;

    // Diagnostics — number of times a layer's blend mode fell back to
    // NORMAL because we don't implement it via fixed-function yet.
    [[nodiscard]] auto fallback_count() const noexcept -> std::uint64_t { return fallback_count_; }

    void reset_counters() noexcept { fallback_count_ = 0; }

private:
    LayerCompositor() = default;

    // Pipeline slots — one per fixed-function-supported blend mode.
    // Order pinned by the array indices that blend_state_for() returns.
    enum class Slot : std::uint8_t {
        normal = 0,
        screen = 1,
        linear_dodge = 2,
        multiply = 3,
        count
    };

    [[nodiscard]] static auto slot_for(noted::domain::BlendMode mode,
                                       bool& supported_out) noexcept -> Slot;

    // Lazy one-shot: on the first composite() call after create(),
    // record a clear-to-white on the dummy mask and transition it to
    // SHADER_READ_ONLY_OPTIMAL. Subsequent calls skip this.
    void ensure_dummy_initialized_(VkCommandBuffer cb) noexcept;

    // Member declaration order is significant — destruction runs in
    // reverse declaration order and Vulkan's resource lifetime rules
    // dictate the safe order:
    //   1) VkPipeline objects must die before VkPipelineLayout.
    //   2) VkDescriptorSets must die (via pool destruction) before
    //      their VkDescriptorSetLayout.
    // Hence set_layout_ declared first (dies last), then pipeline_layout_,
    // then descriptor_pool_ (which destroys mask_set_), then the
    // leaf resources, then pipelines_ (dies first).
    std::optional<noted::gpu::DescriptorSetLayout> set_layout_;
    std::optional<noted::gpu::PipelineLayout> pipeline_layout_;
    std::optional<noted::gpu::DescriptorPool> descriptor_pool_;
    noted::gpu::DescriptorSet mask_set_;  // owned by descriptor_pool_
    std::optional<noted::gpu::Sampler> sampler_;
    // 1×1 R8 "all selected" mask bound when the caller passes nullptr.
    std::optional<noted::gpu::SelectionMask> dummy_mask_;
    std::array<std::optional<noted::gpu::GraphicsPipeline>, static_cast<std::size_t>(Slot::count)>
        pipelines_;
    bool dummy_initialized_{false};
    std::uint64_t fallback_count_{0};
};

}  // namespace noted::compositor
