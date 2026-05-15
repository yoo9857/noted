#pragma once

// Stroke engine — the input-→-pixel path.
//
// Subscribes to engine-wide pointer hook channels. Tracks drag state.
// While the left button is held, every PointerMoved event accumulates a
// disk stamp (center, radius, color) in an internal vector. Each frame
// the renderer asks the engine to record() the stamps into the canvas
// via the stamp pipeline.
//
// MVP simplifications (replace in later PRs):
//   - Re-renders ALL accumulated stamps every frame instead of an
//     incremental LOAD_OP_LOAD canvas. Trade-off: per-frame cost grows
//     with stamp count. Acceptable for a few thousand stamps; below
//     that threshold the simpler model wins. The proper fix is the
//     compositor's job (P3 #8).
//   - Single brush: constant radius + opaque black color. Pressure /
//     tilt / per-stroke styling land with feat/stroke-engine-pressure
//     (P2 #6) once feat/pen-input lands.
//   - No undo/redo. The Command stack ties in at P4 #11.
//
// Rationale: see docs/architecture/0015-stroke-engine-mvp.md.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
// PipelineLayout / GraphicsPipeline are held by-value (in optional) so they
// need the full definitions. Device / ShaderModule / Registry only appear
// as pointers in CreateInfo — forward declarations below keep the header's
// transitive include footprint small.
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/hook/hook.hpp"

namespace noted::gpu {
class Device;
class ShaderModule;
}  // namespace noted::gpu

namespace noted::hook {
class Registry;
}  // namespace noted::hook

namespace noted::stroke {

// Public POD describing a single brush stamp.
//
// Coordinates are pixel-space, top-left origin, matching GLFW pointer
// events. Color is straight-alpha RGBA; the pipeline blends it onto the
// canvas with SRC_ALPHA / ONE_MINUS_SRC_ALPHA.
struct Stamp {
    float x_px       = 0.0F;
    float y_px       = 0.0F;
    float radius_px  = 4.0F;
    float softness_px = 1.0F;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

struct StrokeEngineCreateInfo {
    const noted::gpu::Device*         device          = nullptr;
    const noted::gpu::ShaderModule*   vs_module       = nullptr;
    const noted::gpu::ShaderModule*   ps_module       = nullptr;
    VkFormat                          canvas_format   = VK_FORMAT_R8G8B8A8_UNORM;
    noted::hook::Registry*            hook_registry   = nullptr;
};

class StrokeEngine {
public:
    // Returns a heap-allocated engine. Heap allocation is intentional —
    // the engine subscribes to hook channels with `this`-capturing lambdas,
    // so the StrokeEngine address must stay stable for the lifetime of
    // the subscriptions. unique_ptr is the clean expression of that.
    [[nodiscard]] static auto create(const StrokeEngineCreateInfo& info)
        -> Result<std::unique_ptr<StrokeEngine>>;

    // Non-copyable, non-movable. The address is the identity — see create()'s
    // contract. Allocate via create() (heap) or with TestingTag (stack OK
    // because the test-only path doesn't subscribe).
    StrokeEngine(const StrokeEngine&) = delete;
    auto operator=(const StrokeEngine&) -> StrokeEngine& = delete;
    StrokeEngine(StrokeEngine&&) = delete;
    auto operator=(StrokeEngine&&) -> StrokeEngine& = delete;
    ~StrokeEngine() = default;

    // Tell the engine the canvas size in pixels. Must be called once
    // before record() and again on every resize. Cheap.
    void set_canvas_size(VkExtent2D extent) noexcept;

    // Drain-and-draw. Records vkCmdBindPipeline + per-stamp push constants
    // + vkCmdDraw(6, 1, 0, 0) for every accumulated stamp. Caller is
    // responsible for being inside an active vkCmdBeginRendering whose
    // color attachment is the canvas (in COLOR_ATTACHMENT_OPTIMAL).
    void record(VkCommandBuffer cb, VkExtent2D canvas_extent) noexcept;

    // Inspect / control state — used by tests and the future debug UI.
    [[nodiscard]] auto stamp_count() const noexcept -> std::size_t {
        return stamps_.size();
    }
    [[nodiscard]] auto is_drawing() const noexcept -> bool { return drawing_; }
    [[nodiscard]] auto stamps() const noexcept -> const std::vector<Stamp>& {
        return stamps_;
    }
    void clear_stamps() noexcept { stamps_.clear(); }

    // Test-only / no-hook constructor (production code goes through create()).
    // Build an engine with no pipeline + no subscriptions, just the
    // accumulation state. Lets unit tests exercise the pointer-event →
    // Stamp logic without a Vulkan device.
    struct TestingTag {};
    explicit StrokeEngine(TestingTag) noexcept {}

    // Test-only event-injection helpers — mirror what the hook callbacks do.
    void inject_press_(double x, double y, noted::hook::PointerButton b) noexcept;
    void inject_move_(double x, double y) noexcept;
    void inject_release_(double x, double y, noted::hook::PointerButton b) noexcept;
    void inject_resize_(std::uint32_t w, std::uint32_t h) noexcept;

private:
    StrokeEngine() = default;

    void on_pressed(const noted::hook::PointerPressed& e) noexcept;
    void on_moved(const noted::hook::PointerMoved& e) noexcept;
    void on_released(const noted::hook::PointerReleased& e) noexcept;
    void on_resized(const noted::hook::FramebufferResized& e) noexcept;

    // Pipeline + layout — engaged after create() succeeds; disengaged
    // for TestingTag instances that never touch the GPU. The optional
    // wrapping side-steps PipelineLayout's private default constructor.
    std::optional<noted::gpu::PipelineLayout>   layout_;
    std::optional<noted::gpu::GraphicsPipeline> pipeline_;

    // Accumulation state.
    bool                drawing_     = false;
    float               canvas_w_    = 1.0F;
    float               canvas_h_    = 1.0F;
    std::vector<Stamp>  stamps_;

    // RAII subscriptions — released when the engine goes out of scope.
    noted::hook::Subscription<noted::hook::PointerPressed>     sub_pressed_;
    noted::hook::Subscription<noted::hook::PointerMoved>       sub_moved_;
    noted::hook::Subscription<noted::hook::PointerReleased>    sub_released_;
    noted::hook::Subscription<noted::hook::FramebufferResized> sub_resized_;
};

}  // namespace noted::stroke
