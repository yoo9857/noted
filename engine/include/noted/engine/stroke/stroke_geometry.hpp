#pragma once

// Vector-ink stroke geometry — the resolution-independent data model
// that supersedes the SDF disk-stamp accumulation from ADR 0015.
//
// Why vector ink (Phase A.2 of the unified-canvas plan):
//   - Stamps live at the resolution they were drawn at. Zoom in
//     past 1.0 and the antialiased disk turns into a blurry blob.
//     Goodnotes / Procreate avoid this by storing the centerline
//     and re-tessellating per frame.
//   - A vector stroke is roundtrip-stable for the `.noted` file
//     format: pen samples in, pen samples out — no rasterisation
//     loss across save/load.
//   - The ribbon is what the GPU draws. Tessellation is a pure
//     function of the centerline + brush style, so the GPU upload
//     side can cache per stroke and only re-upload on style
//     changes (later PRs).
//
// This header is the **pure data + pure logic** layer. It does
// not touch Vulkan; the GPU integration lives in stroke_engine.
// All math here is `float` (matches the GPU's vertex layout) and
// dimensions are canvas pixels (post-`Camera::unproject`). Unit
// tests verify centerline → ribbon invariants without a GPU.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace noted::stroke {

// How a stroke's pixels combine with whatever is already in the
// strokes target. Snapshotted on each Stroke at press time so a
// user toggling tools mid-document never retroactively rewrites an
// already-committed stroke's behaviour.
//
// `draw`   — normal painter's-algorithm alpha blend.
// `erase`  — destination-out: the target pixel is multiplied by
//            `(1 - src.alpha)`. With the strokes target composited
//            onto the canvas via SRC_OVER, this reveals the paper
//            + layers below — Goodnotes-style eraser.
//
// Wire-stable ordinals — `.noted` schema may persist per-stroke
// mode in a future bump. New modes append; existing values never
// reorder.
enum class DrawMode : std::uint8_t {
    draw = 0,
    erase = 1,
};

// Per-engine brush style.
//
// Fully described tuple: a radius range, an alpha gamma, a softness
// ratio, and a colour. Pressure (0..1) interpolates radius linearly
// between min / max and shapes alpha through pow(pressure,
// alpha_gamma) so a light touch feels noticeably lighter than a
// medium touch.
//
// Defaults yield a ~2..10 px black tip with mild gamma — visible
// but not heavy, matching the demo's textured background.
struct BrushStyle {
    float min_radius_px{2.0F};
    float max_radius_px{10.0F};
    // Softness band as a fraction of the current radius. Carried
    // for future SDF-edge brushes; the ribbon tessellator
    // currently ignores it.
    float softness_ratio{0.20F};
    // pow(pressure, alpha_gamma). 1.0 = linear, >1 emphasizes
    // high pressure, <1 emphasizes light touches.
    float alpha_gamma{1.8F};
    // Straight-alpha color. Alpha is multiplied by the pressure
    // curve; r/g/b pass through unchanged.
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};
};

// Per-sample brush evaluation (legacy name "Stamp" — historically
// this was the rendered primitive; vector ink keeps the type as
// the (radius, colour) tuple the tessellator reads).
//
// `x_px` / `y_px` / `softness_px` are vestigial under the
// vector-ink path. The tessellator only reads
// `radius_px` / `r` / `g` / `b` / `a`.
struct Stamp {
    float x_px = 0.0F;
    float y_px = 0.0F;
    float radius_px = 4.0F;
    float softness_px = 1.0F;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

// Pure mapping: (BrushStyle, pressure) → per-sample brush
// evaluation. Exposed so tests can verify the curve independent
// of the event path and so the tessellator + any future tools
// share one source of truth.
[[nodiscard]] auto stamp_from_pressure(const BrushStyle& style, float pressure) noexcept -> Stamp;

// One pen sample. Captured each frame the pen moves OR pressure
// changes during a drag. Coordinates are canvas pixels — the
// caller (App via `Camera::unproject`) has already converted from
// screen space, so the values are resolution-independent.
struct StrokeSample {
    float x{0.0F};
    float y{0.0F};
    // Pen pressure in [0, 1]. The brush style maps this to a
    // per-sample ribbon width via `stamp_from_pressure`. A mouse
    // (no pressure sensor) supplies 1.0F.
    float pressure{1.0F};
};

// One vector ink stroke. The brush style is captured **at stroke
// start** — later edits to the live BrushStyle do not retroactively
// change accumulated strokes (matching Goodnotes / Photoshop
// expectations: changing the brush mid-document doesn't repaint
// existing ink).
struct Stroke {
    std::vector<StrokeSample> samples{};
    BrushStyle style{};
    // Per-stroke render mode. Set by `StrokeEngine::on_pressed` from
    // the engine's live `mode_` so tool toggling never rewrites a
    // committed stroke's behaviour.
    DrawMode mode{DrawMode::draw};
};

// One ribbon vertex. The tessellator emits these as a TRIANGLE_LIST:
// 6 vertices per segment (= sample pair) forming a rounded-rectangle
// (stadium / capsule) quad. Adjacent segments overlap at their shared
// sample point — both quads contribute a half-disc cap there so the
// join is naturally rounded without explicit miter / bevel logic.
// Crucially this means U-turns / zigzags can't break the ribbon: each
// segment is independent so a sharp direction reversal at sample `i`
// just overlaps two segment quads at sample `i` rather than producing
// a degenerate / self-intersecting triangle strip the way the
// per-sample-strip topology did.
//
// `side` + `t` are the segment-local 2D signed-distance coordinates.
// `K` is the segment's body-to-radius aspect ratio (= L/(2r)) — a
// per-quad constant that lets the fragment shader reconstruct the
// capsule SDF from the normalized (side, t):
//
//   - `side`: perpendicular signed distance, -1 left edge, +1 right.
//   - `t`: tangential signed coordinate spanning the quad, -1 at the
//     start-cap outer rim, +1 at the end-cap outer rim. The body
//     occupies |t| ≤ K / (K + 1).
//   - `K`: half-length of the segment body in radius units. Larger K
//     = longer thin capsule; smaller K = stubby / dot-like.
//
// Fragment-side SDF reconstruction:
//   const float cap = max(0.0, |t|*(K+1) - K);   // 0 in body, >0 in cap
//   const float dist = length(side, cap);        // 1.0 at capsule edge
// `smoothstep(1 - aa, 1, dist)` then produces a 1-pixel AA edge that
// rounds the caps and softens the long sides in a single
// formulation.
struct RibbonVertex {
    float x{0.0F};
    float y{0.0F};
    float r{0.0F};
    float g{0.0F};
    float b{0.0F};
    float a{1.0F};
    float side{0.0F};  // -1 left edge, +1 right edge
    float t{0.0F};     // -1 start-cap rim, +1 end-cap rim
    float K{0.0F};     // body half-length / radius — capsule aspect
};

// Build a triangle-strip ribbon from a stroke's centerline + brush
// style. For each interior sample, the tangent is the average of
// the incoming and outgoing segment directions; for the endpoints
// it's the lone adjacent segment's direction. The ribbon's two
// vertices for sample `i` are `sample ± normal * half_width(i)`.
//
// `half_width(i)` comes from `stamp_from_pressure(style, p_i)` so
// the existing pressure curve (min/max radius + alpha_gamma) is
// the single source of truth.
//
// Degenerate cases:
//   - 0 or 1 sample → empty output (no draw call needed).
//   - Consecutive duplicate samples are skipped so the tangent
//     calculation never divides by zero.
//
// Caller draws with `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP`. The
// returned vector is contiguous + heap-allocated, suitable for
// direct `memcpy` into a `noted::gpu::Buffer`.
[[nodiscard]] auto tessellate_ribbon(const Stroke& stroke) -> std::vector<RibbonVertex>;

}  // namespace noted::stroke
