#include "noted/engine/stroke/stroke_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

// Full definitions for types the header only forward-declares.
#include "noted/engine/gpu/device.hpp"
#include "noted/engine/gpu/shader_module.hpp"
#include "noted/engine/hook/registry.hpp"
#include "noted/engine/profile.hpp"

namespace noted::stroke {

namespace {

// ---- Push-constant payload --------------------------------------------------
// Mirrors the StampPush struct in shaders/stamp.slang. Order MUST match
// (vec4 first, then vec2s, then scalars) so std430 lays it out with zero
// internal padding. static_assert pins the layout to 40 bytes.
struct StampPush {
    float color[4];        // offset  0
    float canvas_size[2];  // offset 16
    float center_px[2];    // offset 24
    float radius_px;       // offset 32
    float softness_px;     // offset 36
};
static_assert(sizeof(StampPush) == 40,
              "StampPush must be 40 bytes — check std430 layout vs stamp.slang");
static_assert(offsetof(StampPush, color) == 0);
static_assert(offsetof(StampPush, canvas_size) == 16);
static_assert(offsetof(StampPush, center_px) == 24);
static_assert(offsetof(StampPush, radius_px) == 32);
static_assert(offsetof(StampPush, softness_px) == 36);

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

    // Gamma curve on alpha. The shader's smoothstep already gives a soft
    // edge, so the gamma's job is purely "light touch → low ink".
    // alpha_gamma <= 0 is treated as 1 (linear) to keep the call safe.
    const float gamma = (style.alpha_gamma > 0.0F) ? style.alpha_gamma : 1.0F;
    const float a = clamp01(style.a * std::pow(p, gamma));

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
    if (info.vs_module == nullptr || info.ps_module == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "StrokeEngine::create: shader modules are null"));
    }
    if (info.hook_registry == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "StrokeEngine::create: hook_registry is null"));
    }

    // Pipeline layout: no descriptor sets, one push-constant range covering
    // both stages (vertex needs position math, fragment needs color/SDF).
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(StampPush);

    auto layout = noted::gpu::PipelineLayout::create(
        *info.device,
        std::span<const noted::gpu::DescriptorSetLayout* const>{},
        std::span<const VkPushConstantRange>{&push_range, 1});
    if (!layout) {
        return std::unexpected(std::move(layout).error());
    }

    // Pipeline: triangle list (6 verts per stamp), no vertex input, no
    // culling, alpha blend (SRC_ALPHA / ONE_MINUS_SRC_ALPHA). Dynamic
    // viewport + scissor inherited from the builder defaults.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
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

    // Allocate on the heap so subscription lambdas can capture a stable
    // `this`. StrokeEngine is non-movable (see header) which makes that
    // promise mechanical: the heap address can never shift.
    auto eng = std::unique_ptr<StrokeEngine>(new StrokeEngine{});
    eng->layout_.emplace(std::move(*layout));
    eng->pipeline_.emplace(std::move(*pipeline));
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

auto StrokeEngine::total_sample_count() const noexcept -> std::size_t {
    std::size_t n = current_stroke_.samples.size();
    for (const auto& s : strokes_) {
        n += s.samples.size();
    }
    return n;
}

void StrokeEngine::clear_strokes() noexcept {
    strokes_.clear();
    current_stroke_ = Stroke{};
}

namespace {

// Helper: emit one stamp draw call. Pulled out of the per-sample
// inner loop in `record` so the loop body reads as data flow
// rather than 12 lines of struct-init boilerplate.
void emit_stamp_draw(VkCommandBuffer cb,
                     VkPipelineLayout layout_h,
                     float cw,
                     float ch,
                     float cx,
                     float cy,
                     const Stamp& brush_eval) noexcept {
    constexpr VkShaderStageFlags kStages =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    StampPush p{};
    p.color[0] = brush_eval.r;
    p.color[1] = brush_eval.g;
    p.color[2] = brush_eval.b;
    p.color[3] = brush_eval.a;
    p.canvas_size[0] = cw;
    p.canvas_size[1] = ch;
    p.center_px[0] = cx;
    p.center_px[1] = cy;
    p.radius_px = brush_eval.radius_px;
    p.softness_px = brush_eval.softness_px;

    vkCmdPushConstants(cb, layout_h, kStages, 0, sizeof(StampPush), &p);
    vkCmdDraw(cb, /*vertexCount=*/6, /*instanceCount=*/1, 0, 0);
}

void draw_stroke_via_stamps(VkCommandBuffer cb,
                            VkPipelineLayout layout_h,
                            float cw,
                            float ch,
                            const Stroke& stroke) noexcept {
    // Per-sample stamp draw — interim rendering path while the
    // vector-ink swap to a polyline ribbon pipeline (Phase A.2.c)
    // is in flight. The Stroke is the durable model; this loop
    // exists so the data-model PR is shippable without the GPU
    // rewrite in the same change.
    for (const auto& sample : stroke.samples) {
        const auto brush_eval = stamp_from_pressure(stroke.style, sample.pressure);
        emit_stamp_draw(cb, layout_h, cw, ch, sample.x, sample.y, brush_eval);
    }
}

}  // namespace

void StrokeEngine::record(VkCommandBuffer cb, VkExtent2D canvas_extent) noexcept {
    NOTED_PROFILE_ZONE_N("StrokeEngine::record");
    if (!pipeline_.has_value() || !layout_.has_value()) {
        return;
    }
    if (strokes_.empty() && current_stroke_.samples.empty()) {
        return;
    }

    const float cw = static_cast<float>(canvas_extent.width == 0 ? 1U : canvas_extent.width);
    const float ch = static_cast<float>(canvas_extent.height == 0 ? 1U : canvas_extent.height);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_->handle());
    const VkPipelineLayout layout_h = layout_->handle();

    for (const auto& stroke : strokes_) {
        draw_stroke_via_stamps(cb, layout_h, cw, ch, stroke);
    }
    if (!current_stroke_.samples.empty()) {
        draw_stroke_via_stamps(cb, layout_h, cw, ch, current_stroke_);
    }
}

// ---- Event handlers ---------------------------------------------------------

void StrokeEngine::on_pressed(const noted::hook::PointerPressed& e) noexcept {
    if (e.button != noted::hook::PointerButton::left) {
        return;
    }
    drawing_ = true;
    // Snapshot the brush style at stroke start — later mid-stroke
    // edits to `brush_` (debug UI sliders, brush presets) do NOT
    // retroactively change this stroke. Matches Goodnotes /
    // Photoshop expectations.
    current_stroke_.style = brush_;
    current_stroke_.samples.clear();
    // Screen → canvas: subtract camera translation, divide by scale.
    // Identity view (default) reduces to s.x = e.x.
    StrokeSample sample{};
    sample.x = static_cast<float>((e.x - view_tx_) / view_scale_);
    sample.y = static_cast<float>((e.y - view_ty_) / view_scale_);
    sample.pressure = e.pressure;
    current_stroke_.samples.push_back(sample);
}

void StrokeEngine::on_moved(const noted::hook::PointerMoved& e) noexcept {
    if (!drawing_) {
        return;
    }
    StrokeSample sample{};
    sample.x = static_cast<float>((e.x - view_tx_) / view_scale_);
    sample.y = static_cast<float>((e.y - view_ty_) / view_scale_);
    sample.pressure = e.pressure;
    current_stroke_.samples.push_back(sample);
}

void StrokeEngine::on_released(const noted::hook::PointerReleased& e) noexcept {
    if (e.button != noted::hook::PointerButton::left) {
        return;
    }
    drawing_ = false;
    // Flush the in-flight stroke into the completed collection
    // when it has anything to draw. Single-sample strokes (no
    // movement between press and release) are dropped — the
    // current ribbon tessellator emits nothing for them anyway,
    // and storing them would just be noise.
    if (current_stroke_.samples.size() >= 2) {
        strokes_.push_back(std::move(current_stroke_));
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
