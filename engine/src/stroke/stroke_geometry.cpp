#include "noted/engine/stroke/stroke_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace noted::stroke {

namespace {

// Filter consecutive duplicate samples so the tangent calculation
// never divides by zero. Two samples are "duplicate" when their
// (x, y) match exactly — pressure differences alone don't make
// adjacent ribbon segments problematic.
[[nodiscard]] auto coalesce(const std::vector<StrokeSample>& src) -> std::vector<StrokeSample> {
    std::vector<StrokeSample> out;
    out.reserve(src.size());
    for (const auto& s : src) {
        if (out.empty() || out.back().x != s.x || out.back().y != s.y) {
            out.push_back(s);
        }
    }
    return out;
}

// Centripetal Catmull-Rom interpolation between p1 and p2 with p0,
// p3 as the surrounding context. t ∈ [0, 1] traces from p1 (t=0)
// to p2 (t=1). The curve is C¹-continuous across samples and
// passes through every original sample.
//
// Pressure interpolates linearly between p1 and p2 — Catmull-Rom on
// pressure too would risk overshoot above 1 or below 0 with sharp
// pressure transitions, and the difference is invisible at typical
// sample rates.
[[nodiscard]] auto catmull_rom_lerp(const StrokeSample& p0,
                                    const StrokeSample& p1,
                                    const StrokeSample& p2,
                                    const StrokeSample& p3,
                                    float t) noexcept -> StrokeSample {
    const float t2 = t * t;
    const float t3 = t2 * t;
    StrokeSample r{};
    r.x = 0.5F * (2.0F * p1.x + (-p0.x + p2.x) * t +
                  (2.0F * p0.x - 5.0F * p1.x + 4.0F * p2.x - p3.x) * t2 +
                  (-p0.x + 3.0F * p1.x - 3.0F * p2.x + p3.x) * t3);
    r.y = 0.5F * (2.0F * p1.y + (-p0.y + p2.y) * t +
                  (2.0F * p0.y - 5.0F * p1.y + 4.0F * p2.y - p3.y) * t2 +
                  (-p0.y + 3.0F * p1.y - 3.0F * p2.y + p3.y) * t3);
    r.pressure = p1.pressure * (1.0F - t) + p2.pressure * t;
    return r;
}

// Replace `samples` with a denser sequence along the Catmull-Rom
// interpolant. Phantom endpoint samples are reflections of the
// adjacent real sample around the endpoint, which gives a natural
// non-overshooting tangent at the stroke's start / end.
[[nodiscard]] auto subdivide(const std::vector<StrokeSample>& samples,
                             int subdivisions) -> std::vector<StrokeSample> {
    if (subdivisions <= 1 || samples.size() < 2U) {
        return samples;
    }
    std::vector<StrokeSample> out;
    out.reserve(samples.size() * static_cast<std::size_t>(subdivisions));
    const auto N = samples.size();
    for (std::size_t i = 0; i + 1U < N; ++i) {
        const StrokeSample& p1 = samples[i];
        const StrokeSample& p2 = samples[i + 1U];

        StrokeSample p0{};
        if (i == 0U) {
            // Reflect p2 around p1 for the phantom "before" sample.
            p0.x = 2.0F * p1.x - p2.x;
            p0.y = 2.0F * p1.y - p2.y;
            p0.pressure = p1.pressure;
        } else {
            p0 = samples[i - 1U];
        }

        StrokeSample p3{};
        if (i + 2U >= N) {
            // Reflect p1 around p2 for the phantom "after" sample.
            p3.x = 2.0F * p2.x - p1.x;
            p3.y = 2.0F * p2.y - p1.y;
            p3.pressure = p2.pressure;
        } else {
            p3 = samples[i + 2U];
        }

        for (int j = 0; j < subdivisions; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(subdivisions);
            out.push_back(catmull_rom_lerp(p0, p1, p2, p3, t));
        }
    }
    // Append the original last sample so the smoothed curve reaches
    // the endpoint exactly.
    out.push_back(samples.back());
    return out;
}

}  // namespace

auto tessellate_ribbon(const Stroke& stroke,
                       int subdivisions_per_segment) -> std::vector<RibbonVertex> {
    auto samples = coalesce(stroke.samples);
    if (samples.size() < 2) {
        return {};
    }
    if (subdivisions_per_segment > 1) {
        samples = subdivide(samples, subdivisions_per_segment);
    }

    std::vector<RibbonVertex> out;
    // 6 verts per segment (TRIANGLE_LIST: 2 triangles per quad).
    out.reserve((samples.size() - 1U) * 6U);

    for (std::size_t i = 0; i + 1U < samples.size(); ++i) {
        const float ax = samples[i].x;
        const float ay = samples[i].y;
        const float bx = samples[i + 1U].x;
        const float by = samples[i + 1U].y;
        const float dx = bx - ax;
        const float dy = by - ay;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0F) {
            continue;  // coalesce should already have eliminated this
        }
        const float tx = dx / len;
        const float ty = dy / len;
        // Perpendicular in screen space (y-down): rotate tangent 90°
        // counterclockwise around (0, 0) → (-ty, tx). "Right" of the
        // tangent direction.
        const float nx = -ty;
        const float ny = tx;

        const auto stamp_a = stamp_from_pressure(stroke.style, samples[i].pressure);
        const auto stamp_b = stamp_from_pressure(stroke.style, samples[i + 1U].pressure);
        // One constant radius per segment so the (side, t) SDF stays
        // a uniform capsule. Use the average of the two endpoint
        // pressures' radii — pressure varies slowly across a single
        // segment so the loss of taper is invisible. Per-segment
        // tapering (trapezoidal quad with varying r) is a future
        // refinement; the bigger UX win is the robust topology this
        // per-segment scheme already gives.
        const float r = std::max(0.5F, (stamp_a.radius_px + stamp_b.radius_px) * 0.5F);

        // Aspect ratio K = body-half-length / radius. Body occupies
        // |t| ≤ K/(K+1); caps occupy K/(K+1) < |t| ≤ 1.
        const float K = (len * 0.5F) / r;

        // Quad corners — extended by `r` past each sample along the
        // tangent direction so the fragment-shader SDF has room to
        // draw a rounded cap.
        //   V0 (NW) = A - tangent*r - perp*r
        //   V1 (NE) = A - tangent*r + perp*r
        //   V2 (SW) = B + tangent*r - perp*r
        //   V3 (SE) = B + tangent*r + perp*r
        const float v0x = ax - tx * r - nx * r;
        const float v0y = ay - ty * r - ny * r;
        const float v1x = ax - tx * r + nx * r;
        const float v1y = ay - ty * r + ny * r;
        const float v2x = bx + tx * r - nx * r;
        const float v2y = by + ty * r - ny * r;
        const float v3x = bx + tx * r + nx * r;
        const float v3y = by + ty * r + ny * r;

        // Colour at each end matches the endpoint stamp's RGBA (so a
        // future pressure-driven colour would gradient-interpolate
        // naturally across the segment); for the current model both
        // stamps share the brush's RGB and only alpha can vary.
        const auto vertex = [](float x, float y, float side, float t, float K, const Stamp& s) {
            RibbonVertex v{};
            v.x = x;
            v.y = y;
            v.r = s.r;
            v.g = s.g;
            v.b = s.b;
            v.a = s.a;
            v.side = side;
            v.t = t;
            v.K = K;
            return v;
        };

        // Triangle 1: V0, V1, V2  (CCW under y-down screen)
        out.push_back(vertex(v0x, v0y, -1.0F, -1.0F, K, stamp_a));
        out.push_back(vertex(v1x, v1y, +1.0F, -1.0F, K, stamp_a));
        out.push_back(vertex(v2x, v2y, -1.0F, +1.0F, K, stamp_b));
        // Triangle 2: V1, V3, V2  (shares edge V1-V2 with triangle 1)
        out.push_back(vertex(v1x, v1y, +1.0F, -1.0F, K, stamp_a));
        out.push_back(vertex(v3x, v3y, +1.0F, +1.0F, K, stamp_b));
        out.push_back(vertex(v2x, v2y, -1.0F, +1.0F, K, stamp_b));
    }
    return out;
}

}  // namespace noted::stroke
