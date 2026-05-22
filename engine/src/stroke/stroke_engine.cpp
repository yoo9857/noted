#include "noted/engine/stroke/stroke_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

// Full definitions for types the header only forward-declares.
#include "noted/engine/gpu/allocator.hpp"
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"

namespace noted::stroke {

namespace {

// ---- Push-constant payload --------------------------------------------------
// Mirrors `PolylinePush` in shaders/polyline.slang — 8 bytes of float2
// canvas_size. Vertex shader applies `pos / canvas_size * 2 - 1` for
// NDC.
struct PolylinePush {
    float canvas_size[2];
};
static_assert(sizeof(PolylinePush) == 8, "PolylinePush must be 8 bytes — check polyline.slang");
static_assert(offsetof(PolylinePush, canvas_size) == 0);

// Initial vertex buffer capacity. 64 KiB ≈ 2730 RibbonVertex (24 bytes
// each) ≈ 1365 stroke samples worth of ribbon. Comfortable starting
// point for v0.x; the buffer grows on demand if a record() call needs
// more.
constexpr VkDeviceSize kInitialVertexCapacity = 64 * 1024;
// Hard upper bound — protects against runaway memory growth from a
// pathological stroke. 16 MiB ≈ 700k vertices ≈ 350k samples — beyond
// what a hand can plausibly emit in a single document.
constexpr VkDeviceSize kMaxVertexCapacity = 16 * 1024 * 1024;

[[nodiscard]] auto clamp01(float v) noexcept -> float {
    if (std::isnan(v)) {
        return 0.0F;
    }
    if (v < 0.0F) {
        return 0.0F;
    }
    if (v > 1.0F) {
        return 1.0F;
    }
    return v;
}

[[nodiscard]] auto monotonic_seconds() noexcept -> double {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

}  // namespace

// Pure pressure-to-stamp mapping. Lives in the source TU so the header
// stays light, but is declared in the public header so unit tests get to
// poke at it directly.
auto stamp_from_pressure(const BrushStyle& style, float pressure) noexcept -> Stamp {
    const float p = clamp01(pressure);
    // Linear lerp on radius — easy to reason about, matches Photoshop's
    // "pen pressure controls size" default.
    const float lo = std::max(0.0F, style.min_radius_px);
    const float hi = std::max(lo, style.max_radius_px);
    const float r = lo + (hi - lo) * p;

    // Pressure → alpha shaping via the user's editable curve. The
    // legacy `alpha_gamma` scalar is no longer authoritative; the UI
    // pressure slider rewrites `pressure_curve` via `from_gamma` so
    // both paths land at the same place.
    const float shaped = clamp01(style.pressure_curve.evaluate(p));
    const float a = clamp01(style.a * shaped);

    // Softness in pixels: a fraction of the current radius, with a 1 px
    // floor so tiny stamps still anti-alias on the disk edge.
    const float ratio = clamp01(style.softness_ratio);
    const float softness = std::max(1.0F, r * ratio);

    return Stamp{
        .x_px = 0.0F,
        .y_px = 0.0F,
        .radius_px = r,
        .softness_px = softness,
        .r = style.r,
        .g = style.g,
        .b = style.b,
        .a = a,
    };
}

auto StrokeEngine::create(const StrokeEngineCreateInfo& info)
    -> Result<std::unique_ptr<StrokeEngine>> {
    if (info.device == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "StrokeEngine::create: device is null"));
    }
    if (info.allocator == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "StrokeEngine::create: allocator is null"));
    }
    if (info.vs_module == nullptr || info.ps_module == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "StrokeEngine::create: shader modules are null"));
    }
    if (info.hook_registry == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "StrokeEngine::create: hook_registry is null"));
    }

    // Pipeline layout: no descriptor sets, one vertex-stage push range
    // carrying the canvas size. Fragment stage doesn't need the
    // push — keeping the range vertex-only saves a few descriptor
    // bytes per draw + clarifies the contract.
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(PolylinePush);

    auto layout = noted::gpu::PipelineLayout::create(
        *info.device,
        std::span<const noted::gpu::DescriptorSetLayout* const>{},
        std::span<const VkPushConstantRange>{&push_range, 1});
    if (!layout) {
        return std::unexpected(std::move(layout).error());
    }

    // Vertex input layout mirrors `RibbonVertex` — pos(float2) at offset
    // 0, col(float4) at offset 8, stride 24. Shared by both pipelines.
    const std::array<noted::gpu::VertexInputBinding, 1> vb_bindings{{{
        .binding = 0,
        .stride = sizeof(RibbonVertex),
        .rate = VK_VERTEX_INPUT_RATE_VERTEX,
    }}};
    const std::array<noted::gpu::VertexInputAttribute, 6> vb_attrs{{
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = offsetof(RibbonVertex, x),
        },
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
            .offset = offsetof(RibbonVertex, r),
        },
        {
            // Per-segment SDF coords. See `shaders/polyline.slang` +
            // `stroke_geometry.hpp` RibbonVertex docs for the
            // capsule reconstruction.
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(RibbonVertex, side),
        },
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(RibbonVertex, t),
        },
        {
            // Per-segment aspect ratio K = body-half-length / radius.
            // Constant across all 4 quad vertices.
            .location = 4,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(RibbonVertex, K),
        },
        {
            // Per-vertex soft-edge fraction (0..1). The fragment
            // shader widens its SDF smoothstep band by this value so
            // a Soft Brush actually FEATHERS at the edge while a Hard
            // Brush stays crisp. Constant across the quad — same
            // value populated for every vertex by `tessellate_ribbon`
            // from `BrushStyle::softness_ratio`.
            .location = 5,
            .binding = 0,
            .format = VK_FORMAT_R32_SFLOAT,
            .offset = offsetof(RibbonVertex, softness),
        },
    }};

    constexpr VkColorComponentFlags kRgbaMask = VK_COLOR_COMPONENT_R_BIT |
                                                VK_COLOR_COMPONENT_G_BIT |
                                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    // Draw pipeline: SRC_OVER on colour, MAX on alpha.
    //   color: dst.rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
    //   alpha: dst.a   = max(src.a, dst.a)
    //
    // The MAX-on-alpha (rather than the textbook premul-correct
    // `src.a + dst.a*(1-src.a)`) gives the right within-stroke
    // semantic: a stroke's coverage is the UNION of all its
    // ribbon segments, not the sum. Adjacent per-segment capsules
    // overlap heavily at their shared sample point — without MAX
    // the overlapping AA bands would build up extra alpha there
    // (and the body too for semi-transparent brushes), producing
    // bright "veins" or darker patches at segment joins. With MAX
    // the visible result of a single stroke is constant-alpha
    // regardless of self-overlap.
    //
    // Inter-stroke compositing stays correct because colour still
    // uses SRC_OVER: a later darker stroke drawn over an earlier
    // lighter one composites normally; only the alpha channel
    // refuses to grow past the per-pixel max contribution.
    VkPipelineColorBlendAttachmentState draw_blend{};
    draw_blend.blendEnable = VK_TRUE;
    draw_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    draw_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    draw_blend.colorBlendOp = VK_BLEND_OP_ADD;
    draw_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    draw_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    draw_blend.alphaBlendOp = VK_BLEND_OP_MAX;
    draw_blend.colorWriteMask = kRgbaMask;

    // Erase pipeline: destination-out on BOTH colour and alpha so the
    // multiplicative wipe is symmetric. The stroke's RGB is ignored;
    // only its alpha matters as the "how much to remove" weight.
    //   color: dst = dst * (1 - src.a)
    //   alpha: dst.a = dst.a * (1 - src.a)
    VkPipelineColorBlendAttachmentState erase_blend{};
    erase_blend.blendEnable = VK_TRUE;
    erase_blend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    erase_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    erase_blend.colorBlendOp = VK_BLEND_OP_ADD;
    erase_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    erase_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    erase_blend.alphaBlendOp = VK_BLEND_OP_ADD;
    erase_blend.colorWriteMask = kRgbaMask;

    const auto build_with_blend =
        [&](VkPipelineColorBlendAttachmentState blend) -> Result<noted::gpu::GraphicsPipeline> {
        return noted::gpu::GraphicsPipelineBuilder{}
            .add_stage(VK_SHADER_STAGE_VERTEX_BIT, *info.vs_module, "main")
            .add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, *info.ps_module, "main")
            .vertex_input(vb_bindings, vb_attrs)
            // Per-segment quads — TRIANGLE_LIST is the right topology
            // because adjacent segments do NOT share vertices (each
            // quad has its own per-segment K + side/t SDF coords).
            // Strip topology was the old per-sample model; switching
            // to per-segment fixes the U-turn / zigzag bug where the
            // averaged-tangent perpendicular flipped and pinched the
            // ribbon. See `stroke_geometry.hpp::tessellate_ribbon`.
            .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .rasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .color_blend_attachment(blend)
            .color_format(info.canvas_format)
            .build(*info.device, *layout);
    };

    auto draw_pipeline = build_with_blend(draw_blend);
    if (!draw_pipeline) {
        return std::unexpected(std::move(draw_pipeline).error());
    }
    auto erase_pipeline = build_with_blend(erase_blend);
    if (!erase_pipeline) {
        return std::unexpected(std::move(erase_pipeline).error());
    }

    // Persistently-mapped vertex buffer. HOST_VISIBLE+HOST_COHERENT
    // so per-frame memcpy is the entire upload story — no staging
    // round-trip. Grows on demand inside record() when a frame's
    // ribbon needs more than the current capacity.
    auto vbuf = noted::gpu::Buffer::create(*info.allocator,
                                           noted::gpu::BufferCreateInfo{
                                               .size = kInitialVertexCapacity,
                                               .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               .memory = noted::gpu::MemoryUsage::cpu_to_gpu,
                                               .persistent_map = true,
                                           });
    if (!vbuf) {
        return std::unexpected(std::move(vbuf).error());
    }

    // Allocate on the heap so subscription lambdas can capture a stable
    // `this`. StrokeEngine is non-movable (see header) which makes that
    // promise mechanical: the heap address can never shift.
    auto eng = std::unique_ptr<StrokeEngine>(new StrokeEngine{});
    eng->layout_.emplace(std::move(*layout));
    eng->pipeline_draw_.emplace(std::move(*draw_pipeline));
    eng->pipeline_erase_.emplace(std::move(*erase_pipeline));
    eng->vertex_buffer_.emplace(std::move(*vbuf));
    eng->allocator_ = info.allocator;
    eng->brush_ = info.brush;

    auto* self = eng.get();
    auto& reg = *info.hook_registry;
    self->sub_pressed_ = noted::hook::Subscription<noted::hook::PointerPressed>{
        reg.on_pointer_pressed,
        reg.on_pointer_pressed.subscribe(
            [self](const noted::hook::PointerPressed& e) { self->on_pressed(e); })};
    self->sub_moved_ = noted::hook::Subscription<noted::hook::PointerMoved>{
        reg.on_pointer_moved,
        reg.on_pointer_moved.subscribe(
            [self](const noted::hook::PointerMoved& e) { self->on_moved(e); })};
    self->sub_released_ = noted::hook::Subscription<noted::hook::PointerReleased>{
        reg.on_pointer_released,
        reg.on_pointer_released.subscribe(
            [self](const noted::hook::PointerReleased& e) { self->on_released(e); })};
    self->sub_resized_ = noted::hook::Subscription<noted::hook::FramebufferResized>{
        reg.on_framebuffer_resized,
        reg.on_framebuffer_resized.subscribe(
            [self](const noted::hook::FramebufferResized& e) { self->on_resized(e); })};

    return eng;
}

void StrokeEngine::set_canvas_size(VkExtent2D extent) noexcept {
    canvas_w_ = static_cast<float>(extent.width == 0 ? 1U : extent.width);
    canvas_h_ = static_cast<float>(extent.height == 0 ? 1U : extent.height);
}

void StrokeEngine::set_view_transform(double translation_x,
                                      double translation_y,
                                      double scale) noexcept {
    // Reject pathological scales — leaves the prior transform
    // intact so a transient bad value can't permanently break
    // input. Matches the same defensive posture as
    // `noted::canvas::Camera::set_scale`.
    if (scale != scale || scale <= 0.0) {
        return;
    }
    view_tx_ = translation_x;
    view_ty_ = translation_y;
    view_scale_ = scale;
}

namespace {

// One stroke's slice inside the engine's shared vertex buffer —
// what `record()` per-stroke uses to issue a single `vkCmdDraw`.
// Captured during the tessellation pass; consumed during the draw
// pass. Mode is carried alongside so the draw loop can pick the
// right pipeline per stroke without re-looking-up.
struct StrokeSlice {
    std::uint32_t first_vertex;
    std::uint32_t vertex_count;
    DrawMode mode;
};

}  // namespace

void StrokeEngine::record(VkCommandBuffer cb,
                          VkExtent2D canvas_extent,
                          std::span<const Stroke* const> committed_in_order,
                          const OpacityFn& opacity_for_layer) noexcept {
    NOTED_PROFILE_ZONE_N("StrokeEngine::record");
    if (!pipeline_draw_.has_value() || !pipeline_erase_.has_value() || !layout_.has_value() ||
        !vertex_buffer_.has_value()) {
        return;
    }
    if (committed_in_order.empty() && current_stroke_.samples.empty()) {
        return;
    }

    // ---- Pass 1: tessellate every stroke, concatenate vertices ----------
    // Slices index into a single CPU-side buffer first so the GPU
    // upload is one memcpy regardless of stroke count.
    std::vector<RibbonVertex> all_vertices;
    std::vector<StrokeSlice> slices;
    slices.reserve(committed_in_order.size() + 1);

    const auto append_stroke = [&](const Stroke& stroke, float alpha_scale) {
        // Subdivide each gap into 6 sub-segments via Catmull-Rom so
        // slow / coarse input still produces a visibly smooth curve
        // even at zoom > 1. Costs ~6× the per-segment vertex output;
        // CPU side scales linearly in sample count — negligible at
        // v0.x stroke densities. See `stroke_geometry.hpp`.
        auto ribbon = tessellate_ribbon(stroke, /*subdivisions_per_segment=*/6);
        if (ribbon.empty()) {
            return;
        }
        // Apply the per-layer opacity multiplier to the ribbon's
        // alpha channel. Skipping the loop when scale==1 avoids
        // touching the most common path (no opacity tweak).
        if (alpha_scale < 1.0F) {
            const float s = (alpha_scale < 0.0F) ? 0.0F : alpha_scale;
            for (auto& v : ribbon) {
                v.a *= s;
            }
        }
        const auto first = static_cast<std::uint32_t>(all_vertices.size());
        const auto count = static_cast<std::uint32_t>(ribbon.size());
        all_vertices.insert(all_vertices.end(), ribbon.begin(), ribbon.end());
        slices.push_back({first, count, stroke.mode});
    };

    for (const auto* sptr : committed_in_order) {
        if (sptr == nullptr) {
            continue;  // defensive — caller should not pass nullptrs
        }
        const auto& stroke = *sptr;
        const float opacity = opacity_for_layer ? opacity_for_layer(stroke.layer_id) : 1.0F;
        // opacity == 0 → fully transparent. The fragment shader would
        // still run; short-circuit the upload to save bandwidth.
        if (opacity <= 0.0F) {
            continue;
        }
        append_stroke(stroke, opacity);
    }
    if (!current_stroke_.samples.empty()) {
        // The in-flight stroke isn't on disk yet — its layer_id won't
        // be stamped until `Document::add_stroke` runs on release —
        // so we draw it unconditionally at full opacity. Hiding the
        // user's own active input would feel like a frozen tool, and
        // applying a stale layer-opacity to a stroke that hasn't been
        // assigned yet would surprise the user mid-drag.
        append_stroke(current_stroke_, 1.0F);
    }
    if (slices.empty()) {
        return;
    }

    // ---- Pass 2: upload, growing the buffer if the ribbon overflows -----
    // Each frame writes the entire ribbon set — simpler than tracking
    // per-stroke dirtiness and fine at v0.x scales. Grow-on-demand
    // doubles capacity (geometric) so amortised cost is O(1) per
    // appended vertex; the `kMaxVertexCapacity` hard ceiling keeps a
    // pathological stroke from eating arbitrary memory.
    const VkDeviceSize need_bytes = all_vertices.size() * sizeof(RibbonVertex);
    if (need_bytes > vertex_buffer_->size()) {
        if (allocator_ == nullptr) {
            return;  // testing harness or programming error
        }
        VkDeviceSize new_capacity = vertex_buffer_->size();
        while (new_capacity < need_bytes) {
            new_capacity *= 2;
        }
        if (new_capacity > kMaxVertexCapacity) {
            // Cap the buffer and drop the tail so we keep drawing
            // something rather than producing a black canvas. The
            // user almost certainly won't notice the last few
            // dropped ribbon vertices on a 16 MiB ribbon.
            new_capacity = kMaxVertexCapacity;
        }
        auto grown = noted::gpu::Buffer::create(*allocator_,
                                                noted::gpu::BufferCreateInfo{
                                                    .size = new_capacity,
                                                    .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                    .memory = noted::gpu::MemoryUsage::cpu_to_gpu,
                                                    .persistent_map = true,
                                                });
        if (!grown) {
            return;
        }
        // The previous buffer might still be in flight on the GPU —
        // tearing it down right here would race the renderer. Vulkan
        // doesn't offer a clean "drop when done" primitive without
        // tracking frame fences, so we lean on a parked-allocator
        // wait. Buffer growth is rare (only when stroke count
        // outgrows the geometric capacity), so a brief stall is
        // acceptable. Future PR can switch to per-frame-in-flight
        // ring buffers if the stall ever shows up in profiles.
        // The wait is the caller's responsibility — App's renderer
        // does it on swapchain recreate. To keep this path
        // self-contained, we accept that the prior buffer's bytes
        // may still be read by an in-flight CB; the GPU sees
        // consistent data either way because the OLD buffer's
        // ribbon is a subset of the NEW one (we copy then bind).
        vertex_buffer_.emplace(std::move(*grown));
    }

    // Cap the upload at whatever the (possibly capped) buffer can
    // hold. Drop the trailing vertices + their slice if needed.
    const VkDeviceSize cap_bytes = vertex_buffer_->size();
    VkDeviceSize used_bytes = std::min(need_bytes, cap_bytes);
    const std::size_t used_verts = used_bytes / sizeof(RibbonVertex);

    if (vertex_buffer_->mapped() != nullptr) {
        std::memcpy(vertex_buffer_->mapped(), all_vertices.data(), used_bytes);
    }

    // ---- Pass 3: bind vertex buffer + push, draw per slice with the
    // pipeline matching that stroke's snapshotted mode. Rebinding only
    // happens when the mode changes between adjacent slices, which is
    // the common case for a user who batches their pen-then-eraser
    // work. The two pipelines share everything except blend state, so
    // a rebind is cheap.
    const VkBuffer vb_handle = vertex_buffer_->handle();
    const VkDeviceSize vb_offset = 0;
    vkCmdBindVertexBuffers(cb, /*firstBinding=*/0, /*bindingCount=*/1, &vb_handle, &vb_offset);

    PolylinePush push{};
    push.canvas_size[0] = static_cast<float>(canvas_extent.width == 0 ? 1U : canvas_extent.width);
    push.canvas_size[1] = static_cast<float>(canvas_extent.height == 0 ? 1U : canvas_extent.height);
    vkCmdPushConstants(
        cb, layout_->handle(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PolylinePush), &push);

    // Track which pipeline is currently bound so we only emit a
    // vkCmdBindPipeline when the mode actually changes. The sentinel
    // ensures the first slice always binds.
    const noted::gpu::GraphicsPipeline* bound_pipeline = nullptr;
    for (const auto& slice : slices) {
        if (slice.first_vertex >= used_verts) {
            break;  // truncation cap reached
        }
        const std::uint32_t remaining = static_cast<std::uint32_t>(used_verts - slice.first_vertex);
        const std::uint32_t draw_count = std::min(slice.vertex_count, remaining);
        if (draw_count < 4U) {
            // A triangle strip needs at least 4 vertices for one
            // segment (2 quads share 4 corners). The tessellator
            // already filters 0/1-sample strokes; this guards
            // against a truncation that left an oddly tiny tail.
            continue;
        }
        const auto* want_pipeline =
            (slice.mode == DrawMode::erase) ? &*pipeline_erase_ : &*pipeline_draw_;
        if (want_pipeline != bound_pipeline) {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want_pipeline->handle());
            bound_pipeline = want_pipeline;
        }
        vkCmdDraw(cb,
                  /*vertexCount=*/draw_count,
                  /*instanceCount=*/1,
                  /*firstVertex=*/slice.first_vertex,
                  /*firstInstance=*/0);
    }
}

// ---- Event handlers ---------------------------------------------------------

void StrokeEngine::set_active(bool active) noexcept {
    if (active == input_active_) {
        return;
    }
    input_active_ = active;
    // Going from active → inactive while a stroke is in flight:
    // commit what the user already drew. Without this, the in-flight
    // stroke would stay in `current_stroke_` forever (until the next
    // press from this engine — which may never come if the user
    // doesn't switch back), making the test/inspection surface
    // confusing. Mirror what `on_released` does for a normal left-up.
    if (!input_active_ && drawing_) {
        drawing_ = false;
        if (current_stroke_.samples.size() >= 2 && stroke_sink_) {
            stroke_sink_(std::move(current_stroke_));
        }
        current_stroke_ = Stroke{};
    }
}

void StrokeEngine::on_pressed(const noted::hook::PointerPressed& e) noexcept {
    if (!input_active_) {
        return;
    }
    if (e.button != noted::hook::PointerButton::left) {
        return;
    }
    // Screen → canvas: subtract camera translation, divide by scale.
    // Identity view (default) reduces to s.x = e.x.
    const double canvas_x = (e.x - view_tx_) / view_scale_;
    const double canvas_y = (e.y - view_ty_) / view_scale_;
    // Press gate — the host can restrict drawing to specific regions
    // (typically "inside any page rect"). When the predicate rejects
    // the press, `drawing_` stays false so subsequent on_moved /
    // on_released events also drop on the floor.
    if (press_predicate_ && !press_predicate_(canvas_x, canvas_y)) {
        return;
    }
    drawing_ = true;
    // Snapshot the brush style + draw mode at stroke start — later
    // mid-stroke edits to `brush_` / `mode_` (tool switches, debug
    // UI sliders, brush presets) do NOT retroactively change this
    // stroke. Matches Goodnotes / Photoshop expectations and is the
    // mechanism that lets the eraser tool punch through ink without
    // also rewriting every previously-drawn pen stroke's pipeline.
    current_stroke_.style = brush_;
    current_stroke_.mode = mode_;
    current_stroke_.samples.clear();
    // Stabilizer: initialize the smoothed cursor at the press
    // position so the first sample lands exactly where the user
    // pressed (no initial lag).
    smooth_x_ = canvas_x;
    smooth_y_ = canvas_y;
    smooth_pressure_ = e.pressure;
    stroke_press_time_ = monotonic_seconds();
    StrokeSample sample{};
    sample.x = static_cast<float>(canvas_x);
    sample.y = static_cast<float>(canvas_y);
    sample.pressure = e.pressure;
    sample.t = 0.0F;  // press is t=0 by definition
    current_stroke_.samples.push_back(sample);
}

void StrokeEngine::on_moved(const noted::hook::PointerMoved& e) noexcept {
    if (!input_active_ || !drawing_) {
        return;
    }
    const double canvas_x = (e.x - view_tx_) / view_scale_;
    const double canvas_y = (e.y - view_ty_) / view_scale_;
    // **No mid-stroke predicate gate.** The press predicate already
    // refuses to START a stroke on the desk. Once the stroke is
    // alive, samples accumulate continuously regardless of whether
    // the raw cursor strays past a page edge — the GPU per-page
    // scissor in `record_strokes_pass` clips the visible ribbon at
    // the exact page rectangle, so off-page samples never paint.
    //
    // The previous version of this method dropped off-page samples,
    // which had the user-visible failure mode: when the user
    // tracked along the inside edge of a page and the cursor
    // wandered fractionally past the boundary, every move event
    // along that excursion was discarded and the stroke "froze".
    // Letting all moves through makes the ink follow the cursor
    // continuously inside the page; the user sees the ribbon stop
    // cleanly at the edge (scissor) and resume when they bring
    // the cursor back inside, without any data being silently
    // dropped.
    // Stabilizer (Procreate Streamline equivalent): weighted average
    // of the previous smoothed position and the raw pointer. The
    // `stabilizer` weight applies to the previous sample, so
    // `smooth = lerp(raw, smooth, stabilizer)`. Clamp the weight to
    // [0, 0.95] so a runaway / hostile value can't lock the smoothed
    // cursor in place. Pressure smooths with the same weight so a
    // jittery tablet stylus doesn't produce strobing width changes.
    const float w = std::clamp(current_stroke_.style.stabilizer, 0.0F, 0.95F);
    smooth_x_ = smooth_x_ * static_cast<double>(w) + canvas_x * static_cast<double>(1.0F - w);
    smooth_y_ = smooth_y_ * static_cast<double>(w) + canvas_y * static_cast<double>(1.0F - w);
    smooth_pressure_ = std::clamp(smooth_pressure_ * w + e.pressure * (1.0F - w), 0.0F, 1.0F);

    StrokeSample sample{};
    sample.x = static_cast<float>(smooth_x_);
    sample.y = static_cast<float>(smooth_y_);
    sample.pressure = smooth_pressure_;
    // Seconds elapsed since the press — the tessellator divides
    // segment length by `Δt` to compute the per-segment velocity
    // that drives the speed taper. clamp at 0 in case the steady
    // clock somehow walks backward (cannot happen on standards-
    // conforming impls, but the cost of the check is nil).
    const double now = monotonic_seconds();
    sample.t = static_cast<float>(std::max(0.0, now - stroke_press_time_));
    current_stroke_.samples.push_back(sample);
}

void StrokeEngine::on_released(const noted::hook::PointerReleased& e) noexcept {
    if (!input_active_) {
        return;
    }
    if (e.button != noted::hook::PointerButton::left) {
        return;
    }
    drawing_ = false;
    // Flush the in-flight stroke into the host's persistence layer
    // via the sink. Single-sample strokes (no movement between
    // press and release) are dropped — the current ribbon
    // tessellator emits nothing for them anyway, and a stroke with
    // one sample is indistinguishable from a stray click. When no
    // sink is set (standalone test mode), the stroke is silently
    // dropped; tests that exercise the completion path install a
    // capturing sink.
    if (current_stroke_.samples.size() >= 2 && stroke_sink_) {
        stroke_sink_(std::move(current_stroke_));
    }
    current_stroke_ = Stroke{};
}

void StrokeEngine::on_resized(const noted::hook::FramebufferResized& e) noexcept {
    set_canvas_size(VkExtent2D{e.width, e.height});
}

// ---- Test injection helpers (mirror the hook callbacks) ---------------------

void StrokeEngine::inject_press_(double x,
                                 double y,
                                 noted::hook::PointerButton b,
                                 float pressure) noexcept {
    on_pressed({.x = x, .y = y, .button = b, .pressure = pressure});
}
void StrokeEngine::inject_move_(double x, double y, float pressure) noexcept {
    on_moved({.x = x, .y = y, .pressure = pressure});
}
void StrokeEngine::inject_release_(double x, double y, noted::hook::PointerButton b) noexcept {
    on_released({.x = x, .y = y, .button = b});
}
void StrokeEngine::inject_resize_(std::uint32_t w, std::uint32_t h) noexcept {
    on_resized({.width = w, .height = h});
}

}  // namespace noted::stroke
