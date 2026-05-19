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
#include <utility>
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
    // Upper bound on frames-in-flight the compositor will rotate
    // descriptor sets through. Two is the renderer default; we leave
    // a margin so a triple-buffer experiment doesn't need a layout
    // change. If the renderer ever asks for more, create() rejects.
    static constexpr std::uint32_t kMaxFramesInFlight = 4;

    struct CreateInfo {
        const noted::gpu::Allocator* allocator;  // for the dummy mask
        const noted::gpu::Device* device;
        const noted::gpu::ShaderModule* vs_module;
        const noted::gpu::ShaderModule* ps_module;
        VkFormat canvas_format;
        // Graphics queue + family used to immediately submit the dummy
        // mask's one-time clear-to-white during create(). Required —
        // create() returns invalid_argument if either is unset.
        VkQueue graphics_queue = VK_NULL_HANDLE;
        std::uint32_t graphics_family = UINT32_MAX;
        // Number of CPU-side frames the renderer keeps in flight. The
        // compositor allocates one descriptor set per slot and round-
        // robins through them so the descriptor write at composite()
        // never targets a set the GPU is still reading. Must equal the
        // renderer's frames_in_flight(); a mismatch produces the same
        // validation error this design exists to prevent.
        std::uint32_t frames_in_flight = 2;
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
    // **Frame-loop safe:** this call records only draw commands and at
    // most one descriptor write (skipped when the bound view matches
    // last frame's). It performs no barriers and no clears, so it is
    // legal to call between `vkCmdBeginRendering` and `vkCmdEndRendering`.
    // The dummy mask is fully initialized in create(); call ordering
    // requirements are documented there.
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

    // Per-frame state for descriptor rotation. One slot per
    // frame-in-flight: the descriptor set bound at composite() and a
    // cache of the VkImageView the slot was last written with. When
    // the next frame wants to bind the same view, we skip the
    // vkUpdateDescriptorSets call entirely — common case is "same
    // dummy mask every frame", in which case after the warm-up rounds
    // we record zero descriptor writes per frame.
    struct FrameSlot {
        noted::gpu::DescriptorSet set;  // owned by descriptor_pool_
        VkImageView cached_view{VK_NULL_HANDLE};
    };

    // Member declaration order is significant — destruction runs in
    // reverse declaration order and Vulkan's resource lifetime rules
    // dictate the safe order:
    //   1) VkPipeline objects must die before VkPipelineLayout.
    //   2) VkDescriptorSets must die (via pool destruction) before
    //      their VkDescriptorSetLayout.
    // Hence set_layout_ declared first (dies last), then pipeline_layout_,
    // then descriptor_pool_ (which destroys all FrameSlot::set handles),
    // then the leaf resources, then pipelines_ (dies first).
    std::optional<noted::gpu::DescriptorSetLayout> set_layout_;
    std::optional<noted::gpu::PipelineLayout> pipeline_layout_;
    std::optional<noted::gpu::DescriptorPool> descriptor_pool_;
    std::vector<FrameSlot> frame_slots_;  // size == frames_in_flight from create()
    std::optional<noted::gpu::Sampler> sampler_;
    // 1×1 R8 "all selected" mask bound when the caller passes nullptr.
    // Cleared to 1.0 + transitioned to SHADER_READ_ONLY_OPTIMAL at
    // create() time via gpu::immediate_submit — never touched again.
    std::optional<noted::gpu::SelectionMask> dummy_mask_;
    std::array<std::optional<noted::gpu::GraphicsPipeline>, static_cast<std::size_t>(Slot::count)>
        pipelines_;
    // Monotonic counter — `frame_counter_ % frame_slots_.size()` picks
    // which slot composite() uses this call. Mod-rotates correctly even
    // when the renderer's fence wait has guaranteed prior use is done.
    std::uint64_t frame_counter_{0};
    std::uint64_t fallback_count_{0};
};

}  // namespace noted::compositor
