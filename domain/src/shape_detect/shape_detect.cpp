#include "noted/domain/shape_detect/shape_detect.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace noted::domain::shape_detect {

namespace {

// ---- Defensive helpers ----------------------------------------------------

[[nodiscard]] auto is_finite(float v) noexcept -> bool {
    return std::isfinite(v);
}

[[nodiscard]] auto is_finite(const noted::stroke::StrokeSample& s) noexcept -> bool {
    return is_finite(s.x) && is_finite(s.y);
}

[[nodiscard]] auto all_finite(const std::vector<noted::stroke::StrokeSample>& samples) noexcept
    -> bool {
    for (const auto& s : samples) {
        if (!is_finite(s)) {
            return false;
        }
    }
    return true;
}

// ---- Geometric primitives -------------------------------------------------

struct AABB {
    float x_min{0.0F};
    float y_min{0.0F};
    float x_max{0.0F};
    float y_max{0.0F};

    [[nodiscard]] auto width() const noexcept -> float { return x_max - x_min; }
    [[nodiscard]] auto height() const noexcept -> float { return y_max - y_min; }
    [[nodiscard]] auto diagonal() const noexcept -> float {
        const float w = width();
        const float h = height();
        return std::sqrt(w * w + h * h);
    }
    [[nodiscard]] auto cx() const noexcept -> float { return (x_min + x_max) * 0.5F; }
    [[nodiscard]] auto cy() const noexcept -> float { return (y_min + y_max) * 0.5F; }
};

[[nodiscard]] auto compute_bbox(const std::vector<noted::stroke::StrokeSample>& samples) noexcept
    -> AABB {
    AABB out{};
    out.x_min = samples.front().x;
    out.y_min = samples.front().y;
    out.x_max = samples.front().x;
    out.y_max = samples.front().y;
    for (const auto& s : samples) {
        out.x_min = std::min(out.x_min, s.x);
        out.y_min = std::min(out.y_min, s.y);
        out.x_max = std::max(out.x_max, s.x);
        out.y_max = std::max(out.y_max, s.y);
    }
    return out;
}

// Mean perpendicular distance from `samples` to the unit ellipse
// inscribed in `bbox`. An ellipse-normalized coordinate
// (dx/rx, dy/ry) sits on the unit circle when the sample is on the
// ellipse, so the deviation is |length(normalized) - 1|. Scaling
// back to pixels gives mean perpendicular pixel deviation.
[[nodiscard]] auto mean_ellipse_deviation_px(
    const std::vector<noted::stroke::StrokeSample>& samples, const AABB& bbox) noexcept -> float {
    const float rx = std::max(0.5F, bbox.width() * 0.5F);
    const float ry = std::max(0.5F, bbox.height() * 0.5F);
    const float cx = bbox.cx();
    const float cy = bbox.cy();
    // The radial-distance error in normalized units is
    // |length((dx/rx, dy/ry)) - 1|. Scaling that error back to
    // pixels uses the geometric mean of rx and ry — a reasonable
    // axis-aware proxy that doesn't favour one axis when the
    // ellipse is non-circular.
    const float r_scale = std::sqrt(rx * ry);
    float sum = 0.0F;
    for (const auto& s : samples) {
        const float dx = (s.x - cx) / rx;
        const float dy = (s.y - cy) / ry;
        const float d = std::sqrt(dx * dx + dy * dy);
        sum += std::fabs(d - 1.0F);
    }
    return (sum / static_cast<float>(samples.size())) * r_scale;
}

// Mean Chebyshev distance from `samples` to the closest side of the
// rectangle = `bbox`. For an axis-aligned rectangle the perpendicular
// distance from a point to the closest side is
//   min(dist_to_left, dist_to_right, dist_to_top, dist_to_bottom)
// EXCEPT when the point is outside the rectangle, where the L∞
// outside-distance dominates. Strokes intended as rectangles trace
// the perimeter so all samples are near (not inside) the box; this
// formula treats both sides cleanly.
[[nodiscard]] auto mean_rectangle_deviation_px(
    const std::vector<noted::stroke::StrokeSample>& samples, const AABB& bbox) noexcept -> float {
    float sum = 0.0F;
    for (const auto& s : samples) {
        const float dx_left = std::fabs(s.x - bbox.x_min);
        const float dx_right = std::fabs(s.x - bbox.x_max);
        const float dy_top = std::fabs(s.y - bbox.y_min);
        const float dy_bottom = std::fabs(s.y - bbox.y_max);
        // Distance to the nearest of the four sides — inside or
        // outside, the closest perpendicular drop to a side is the
        // valid "how far from the rectangle outline" measure.
        const float side_dist = std::min({dx_left, dx_right, dy_top, dy_bottom});
        sum += side_dist;
    }
    return sum / static_cast<float>(samples.size());
}

// Distance between two samples in canvas pixels.
[[nodiscard]] auto sample_distance(const noted::stroke::StrokeSample& a,
                                   const noted::stroke::StrokeSample& b) noexcept -> float {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Build a ShapePrimitive of the given kind from an AABB + the
// originating stroke's brush colour / size. Centralises the
// snapshot rule so the two detectors stay symmetric.
[[nodiscard]] auto make_primitive(const AABB& bbox,
                                  noted::domain::tool::ShapeKind kind,
                                  const noted::stroke::BrushStyle& style) noexcept
    -> noted::domain::tool::ShapePrimitive {
    noted::domain::tool::ShapePrimitive p{};
    p.kind = kind;
    p.x0 = static_cast<double>(bbox.x_min);
    p.y0 = static_cast<double>(bbox.y_min);
    p.x1 = static_cast<double>(bbox.x_max);
    p.y1 = static_cast<double>(bbox.y_max);
    p.stroke_r = style.r;
    p.stroke_g = style.g;
    p.stroke_b = style.b;
    p.stroke_a = style.a;
    // Brush's max radius is the visible outline width when the user
    // pressed hard; using the max gives a stroke that reads at the
    // same weight as the freehand input would have. Floor at 0.5 px
    // matches `shape_from_drag`'s discipline.
    p.stroke_width_px = std::max(0.5F, style.max_radius_px);
    return p;
}

// Pre-screen common rejection criteria so the detectors stay focused
// on their own geometry. Returns the cleaned sample list (or empty
// if any precondition fails).
[[nodiscard]] auto prepare_samples(const noted::stroke::Stroke& stroke,
                                   const DetectionConfig& cfg) noexcept
    -> std::vector<noted::stroke::StrokeSample> {
    if (stroke.samples.size() < static_cast<std::size_t>(std::max(2, cfg.min_samples))) {
        return {};
    }
    if (!all_finite(stroke.samples)) {
        return {};
    }
    return stroke.samples;
}

}  // namespace

// ---- Public API -----------------------------------------------------------

auto detect_ellipse(const noted::stroke::Stroke& stroke,
                    const DetectionConfig& cfg) -> std::optional<DetectionResult> {
    const auto samples = prepare_samples(stroke, cfg);
    if (samples.empty()) {
        return std::nullopt;
    }
    const auto bbox = compute_bbox(samples);
    const float diag = bbox.diagonal();
    if (diag < cfg.min_bbox_diagonal_px) {
        return std::nullopt;
    }
    // Closure check — ellipses are closed.
    const float closure_gap = sample_distance(samples.front(), samples.back());
    if (closure_gap > cfg.closure_ratio * diag) {
        return std::nullopt;
    }

    const float mean_dev_px = mean_ellipse_deviation_px(samples, bbox);
    if (mean_dev_px > cfg.tolerance_px) {
        return std::nullopt;
    }

    DetectionResult r{};
    r.shape = make_primitive(bbox, noted::domain::tool::ShapeKind::ellipse, stroke.style);
    // Map deviation [0, tolerance] → confidence [1, 0]. Strokes
    // hugging the ellipse closely score near 1; ones at the
    // tolerance edge score near 0. Floor at 0 to guard rounding.
    r.confidence = std::max(0.0F, 1.0F - mean_dev_px / cfg.tolerance_px);
    return r;
}

auto detect_rectangle(const noted::stroke::Stroke& stroke,
                      const DetectionConfig& cfg) -> std::optional<DetectionResult> {
    const auto samples = prepare_samples(stroke, cfg);
    if (samples.empty()) {
        return std::nullopt;
    }
    const auto bbox = compute_bbox(samples);
    const float diag = bbox.diagonal();
    if (diag < cfg.min_bbox_diagonal_px) {
        return std::nullopt;
    }
    const float closure_gap = sample_distance(samples.front(), samples.back());
    if (closure_gap > cfg.closure_ratio * diag) {
        return std::nullopt;
    }

    const float mean_dev_px = mean_rectangle_deviation_px(samples, bbox);
    if (mean_dev_px > cfg.tolerance_px) {
        return std::nullopt;
    }

    DetectionResult r{};
    r.shape = make_primitive(bbox, noted::domain::tool::ShapeKind::rectangle, stroke.style);
    r.confidence = std::max(0.0F, 1.0F - mean_dev_px / cfg.tolerance_px);
    return r;
}

auto detect_shape(const noted::stroke::Stroke& stroke,
                  const DetectionConfig& cfg) -> std::optional<DetectionResult> {
    std::optional<DetectionResult> best{};
    if (auto e = detect_ellipse(stroke, cfg); e) {
        best = e;
    }
    if (auto r = detect_rectangle(stroke, cfg); r) {
        if (!best || r->confidence > best->confidence) {
            best = r;
        }
    }
    if (best && best->confidence < cfg.min_confidence) {
        return std::nullopt;
    }
    return best;
}

}  // namespace noted::domain::shape_detect
