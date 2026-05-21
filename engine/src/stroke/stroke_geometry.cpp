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

}  // namespace

auto tessellate_ribbon(const Stroke& stroke) -> std::vector<RibbonVertex> {
    const auto samples = coalesce(stroke.samples);
    if (samples.size() < 2) {
        return {};
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
