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

// Returns (nx, ny) — the unit perpendicular to (dx, dy). Caller is
// responsible for non-zero input (handled by `coalesce`).
[[nodiscard]] auto unit_perp(float dx, float dy) noexcept -> std::pair<float, float> {
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0F) {
        // Defensive — `coalesce` should have eliminated this case.
        return {0.0F, 0.0F};
    }
    // Perpendicular in screen space (y-down): (-dy, dx) is the
    // left-hand normal, which matches the left/right convention the
    // tessellator uses below (left = sample - normal*w).
    return {-dy / len, dx / len};
}

}  // namespace

auto tessellate_ribbon(const Stroke& stroke) -> std::vector<RibbonVertex> {
    const auto samples = coalesce(stroke.samples);
    if (samples.size() < 2) {
        return {};
    }

    std::vector<RibbonVertex> out;
    out.reserve(samples.size() * 2);

    for (std::size_t i = 0; i < samples.size(); ++i) {
        // Tangent for sample i — average of incoming + outgoing
        // segment directions for interior samples; the lone adjacent
        // segment for the endpoints.
        float dx = 0.0F;
        float dy = 0.0F;
        if (i == 0) {
            dx = samples[1].x - samples[0].x;
            dy = samples[1].y - samples[0].y;
        } else if (i + 1 == samples.size()) {
            dx = samples[i].x - samples[i - 1].x;
            dy = samples[i].y - samples[i - 1].y;
        } else {
            // Average direction vectors. Don't normalize the inputs
            // first — the unit_perp below normalizes the result, so
            // weighting longer segments more is the right behaviour
            // (matches Catmull-Rom-ish tangents).
            dx = (samples[i + 1].x - samples[i - 1].x) * 0.5F;
            dy = (samples[i + 1].y - samples[i - 1].y) * 0.5F;
        }

        const auto [nx, ny] = unit_perp(dx, dy);

        // Reuse the existing pressure curve for half-width + colour.
        // Half-width = stamp radius (the stamp model was already
        // pressure-radius-mapped; the ribbon width is the disc
        // diameter would have been at this sample).
        const auto stamp = stamp_from_pressure(stroke.style, samples[i].pressure);
        const float half_w = stamp.radius_px;

        const float cx = samples[i].x;
        const float cy = samples[i].y;

        RibbonVertex left{};
        left.x = cx - nx * half_w;
        left.y = cy - ny * half_w;
        left.r = stamp.r;
        left.g = stamp.g;
        left.b = stamp.b;
        left.a = stamp.a;

        RibbonVertex right{};
        right.x = cx + nx * half_w;
        right.y = cy + ny * half_w;
        right.r = stamp.r;
        right.g = stamp.g;
        right.b = stamp.b;
        right.a = stamp.a;

        // Triangle-strip order: alternate left, right, left, right.
        // Adjacent strip triples form the two triangles spanning
        // segment (i-1, i): (L_{i-1}, R_{i-1}, L_i) and
        // (R_{i-1}, L_i, R_i). With the perpendicular sign chosen
        // in `unit_perp`, this winds CCW in screen space (y-down).
        out.push_back(left);
        out.push_back(right);
    }
    return out;
}

}  // namespace noted::stroke
