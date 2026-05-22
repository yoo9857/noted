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
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan.h>

#include "noted/engine/error/error.hpp"
// PipelineLayout / GraphicsPipeline / Buffer are held by-value (in
// optional) so they need the full definitions. Device / ShaderModule /
// Allocator / Registry only appear as pointers in CreateInfo — forward
// declarations below keep the header's transitive include footprint
// small.
#include "noted/engine/gpu/buffer.hpp"
#include "noted/engine/gpu/graphics_pipeline.hpp"
#include "noted/engine/gpu/pipeline_layout.hpp"
#include "noted/engine/hook/hook.hpp"
#include "noted/engine/stroke/stroke_geometry.hpp"

namespace noted::gpu {
class Allocator;
class Device;
class ShaderModule;
}  // namespace noted::gpu

namespace noted::hook {
class Registry;
}  // namespace noted::hook

namespace noted::stroke {

// BrushStyle / Stamp / stamp_from_pressure / Stroke / StrokeSample /
// RibbonVertex / tessellate_ribbon / DrawMode all live in
// `stroke_geometry.hpp` — included above. The engine adds the GPU
// pipeline + input-event accumulation; the geometry header owns the
// pure-data + pure-logic side.

struct StrokeEngineCreateInfo {
    // Allocator + device + shader modules are required. The allocator
    // owns the engine's persistently-mapped ribbon vertex buffer; the
    // shader modules are `polyline.vs_polyline` and
    // `polyline.ps_polyline`. The hook registry is what the
    // engine subscribes to for pointer events.
    const noted::gpu::Allocator* allocator = nullptr;
    const noted::gpu::Device* device = nullptr;
    const noted::gpu::ShaderModule* vs_module = nullptr;
    const noted::gpu::ShaderModule* ps_module = nullptr;
    VkFormat canvas_format = VK_FORMAT_R8G8B8A8_UNORM;
    noted::hook::Registry* hook_registry = nullptr;
    BrushStyle brush{};
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

    // Install a screen-pixel → canvas-pixel affine transform applied
    // to every incoming pointer event. Defaults to identity (the
    // pre-camera behavior — stamps land at the GLFW pointer pixel).
    // When the host wires a `noted::canvas::Camera`, pass
    // (cam.translation_x(), cam.translation_y(), cam.scale()) and
    // refresh whenever the camera changes.
    //
    // Inverse is applied as: canvas = (screen - translation) / scale.
    // `scale` of 0 or non-finite is rejected — the prior transform
    // stays intact so a runaway camera state can't render input
    // permanently inert.
    void set_view_transform(double translation_x, double translation_y, double scale) noexcept;

    // Caller-supplied per-layer opacity lookup. Returns the alpha
    // multiplier in [0, 1] for `layer_id`; the engine multiplies the
    // tessellated ribbon's alpha channel by this value before upload.
    // An empty function (the default) means "every stroke at full
    // opacity", matching pre-layer-painting behaviour for callers
    // that don't care about layer opacity yet. Returning `0.0F` is
    // equivalent to hiding the stroke (alpha becomes zero across the
    // ribbon, the GPU short-circuits the fragments).
    //
    // The engine doesn't know about the host's layer-stack data
    // model — the host hands in a closure that resolves the id
    // against its own table. Hidden strokes should be pre-filtered
    // out of `committed_in_order` (the engine never sees them) so
    // this fn is only consulted for strokes whose layer survives the
    // visibility / orphan filter on the caller side.
    using OpacityFn = std::function<float(LayerId)>;

    // Drain-and-draw. For each stroke pointer in `committed_in_order`
    // (intended to be ALREADY SORTED in render order, typically
    // bottom-up by layer) plus the in-flight `current_stroke_`
    // (always drawn at full opacity regardless of layer), tessellates
    // the ribbon, applies layer opacity if `opacity_for_layer` is
    // bound, uploads to the persistently-mapped vertex buffer, and
    // records the per-mode pipeline + draw calls. Caller is
    // responsible for being inside an active vkCmdBeginRendering
    // whose color attachment is the strokes target (in
    // COLOR_ATTACHMENT_OPTIMAL).
    //
    // Passing pointers (rather than the strokes themselves) keeps the
    // host's authoritative `Document::strokes()` storage untouched —
    // re-ordering for layer z-order is one O(n) walk that builds a
    // pointer index, not an O(n) deep-copy of the stroke vector each
    // frame (which would heap-allocate per sample vector). Hidden /
    // orphan strokes simply aren't pushed into the pointer span by
    // the caller, so the engine has no hidden-vs-visible logic.
    void record(VkCommandBuffer cb,
                VkExtent2D canvas_extent,
                std::span<const Stroke* const> committed_in_order,
                const OpacityFn& opacity_for_layer = {}) noexcept;

    // Inspect / control state — used by tests and the future debug UI.
    [[nodiscard]] auto is_drawing() const noexcept -> bool { return drawing_; }
    // The in-flight stroke being accumulated while the pen is held
    // down. `samples` is empty between presses.
    [[nodiscard]] auto current_stroke() const noexcept -> const Stroke& { return current_stroke_; }

    // Live brush style — read freely; mutate when the user changes brush
    // settings. Existing accumulated stamps are unchanged; only future
    // press/move events use the new style.
    [[nodiscard]] auto brush() const noexcept -> const BrushStyle& { return brush_; }
    void set_brush(const BrushStyle& b) noexcept { brush_ = b; }

    // Active draw mode. Mutating swaps which pipeline `record()` binds
    // next frame; in-flight + already-recorded strokes are unaffected.
    // Like `set_brush`, only future presses see the change — the
    // current stroke (if any) finishes in its starting mode.
    [[nodiscard]] auto mode() const noexcept -> DrawMode { return mode_; }
    void set_mode(DrawMode m) noexcept { mode_ = m; }

    // Master input gate. When false, pointer events are dropped on
    // the floor and no new strokes accumulate — the host has
    // delegated input to another tool (selection, shape, text, ...).
    // record() still draws whatever was accumulated, so previously-
    // drawn ink stays visible while a different tool is active.
    //
    // An in-flight stroke is **finalized cleanly** on transition to
    // `false`: the press → move → release sequence the user already
    // started gets committed, then further events are ignored. This
    // avoids dangling drag state across a mid-stroke tool switch.
    [[nodiscard]] auto is_input_active() const noexcept -> bool { return input_active_; }
    void set_active(bool active) noexcept;

    // Press gate — when set, only presses whose canvas coordinates
    // satisfy the predicate start a stroke. The host (App) installs
    // a predicate that asks the document's PageList "is this point
    // inside any page rect?", so drawing is constrained to paper
    // and never lands on the surrounding desk colour. Default is
    // empty (any press starts a stroke) so test-only constructions
    // and standalone use stay simple.
    //
    // Called only on `on_pressed`; mid-stroke `on_moved` samples are
    // never re-validated so a stroke that started inside a page can
    // still trail slightly off the edge (matches Goodnotes feel —
    // the lift after a flick stays continuous).
    using PressPredicate = std::function<bool(double canvas_x, double canvas_y)>;
    void set_press_predicate(PressPredicate p) noexcept { press_predicate_ = std::move(p); }

    // Stroke completion sink. The host installs this to route every
    // released stroke into its persistence layer — typically as an
    // `AddStrokeCommand` against `Document`. When no sink is set
    // (test-only construction), completed strokes are silently
    // dropped; tests that exercise the completion path install a
    // capturing sink.
    //
    // The sink is invoked on the same thread that fires the
    // PointerReleased event (the GLFW event-pump thread); the host
    // is responsible for any cross-thread marshalling if its
    // command stack lives elsewhere.
    using StrokeSink = std::function<void(Stroke completed)>;
    void set_stroke_sink(StrokeSink s) noexcept { stroke_sink_ = std::move(s); }

    // Test-only / no-hook constructor (production code goes through create()).
    // Build an engine with no pipeline + no subscriptions, just the
    // accumulation state. Lets unit tests exercise the pointer-event →
    // Stamp logic without a Vulkan device.
    struct TestingTag {};
    explicit StrokeEngine(TestingTag) noexcept {}
    StrokeEngine(TestingTag, const BrushStyle& b) noexcept : brush_{b} {}

    // Test-only event-injection helpers — mirror what the hook callbacks do.
    void inject_press_(double x,
                       double y,
                       noted::hook::PointerButton b,
                       float pressure = 1.0F) noexcept;
    void inject_move_(double x, double y, float pressure = 1.0F) noexcept;
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
    //
    // Two pipelines, one per `DrawMode`. They share everything except
    // the color-blend attachment state; `record()` picks the right
    // pipeline based on `mode_` each frame. Building both at create()
    // time avoids per-mode lazy initialisation (which would need
    // immediate_submit + a mid-frame wait the first time the user
    // touches the eraser).
    std::optional<noted::gpu::PipelineLayout> layout_;
    std::optional<noted::gpu::GraphicsPipeline> pipeline_draw_;
    std::optional<noted::gpu::GraphicsPipeline> pipeline_erase_;
    // Persistently-mapped ribbon vertex buffer. Each `record()` walks
    // the strokes, tessellates them into `RibbonVertex` triangles,
    // and memcpys into the mapped pointer. The optional lets the
    // TestingTag path skip allocation entirely.
    std::optional<noted::gpu::Buffer> vertex_buffer_;
    // Allocator pointer is non-owning — caller (App) owns the
    // VmaAllocator. Stored so a resize can ask for a new buffer
    // without re-plumbing the create-info.
    const noted::gpu::Allocator* allocator_{nullptr};

    // Accumulation state.
    bool drawing_ = false;
    float canvas_w_ = 1.0F;
    float canvas_h_ = 1.0F;
    // Screen→canvas affine transform applied at input time.
    // Identity by default; updated via set_view_transform().
    double view_tx_ = 0.0;
    double view_ty_ = 0.0;
    double view_scale_ = 1.0;
    // Stroke being accumulated between press and release. After
    // release, `samples` is empty and `style` is reset on the
    // next press from `brush_`. Committed strokes now live on the
    // host's Document (P.S.4); the engine emits them via
    // `stroke_sink_` on release and otherwise has no notion of
    // them.
    Stroke current_stroke_{};
    BrushStyle brush_{};
    DrawMode mode_{DrawMode::draw};
    bool input_active_{true};
    PressPredicate press_predicate_{};
    StrokeSink stroke_sink_{};
    // Stabilizer state — the smoothed cursor lags the raw pointer
    // by an amount controlled by `current_stroke_.style.stabilizer`.
    // Reset each press; updated on every move.
    double smooth_x_{0.0};
    double smooth_y_{0.0};
    float smooth_pressure_{1.0F};
    // Monotonic clock at the most recent press, expressed in seconds.
    // Every sample appended during the same stroke gets its `t`
    // field set to `(now - stroke_press_time_)` so the tessellator
    // can compute per-segment velocity for the speed-taper dynamic.
    double stroke_press_time_{0.0};

    // RAII subscriptions — released when the engine goes out of scope.
    noted::hook::Subscription<noted::hook::PointerPressed> sub_pressed_;
    noted::hook::Subscription<noted::hook::PointerMoved> sub_moved_;
    noted::hook::Subscription<noted::hook::PointerReleased> sub_released_;
    noted::hook::Subscription<noted::hook::FramebufferResized> sub_resized_;
};

}  // namespace noted::stroke
