#include "noted/domain/shape_detect/shape_detect.hpp"

#include <cmath>
#include <numbers>
#include <vector>

#include <gtest/gtest.h>

namespace {

using noted::domain::shape_detect::detect_ellipse;
using noted::domain::shape_detect::detect_rectangle;
using noted::domain::shape_detect::detect_shape;
using noted::domain::shape_detect::DetectionConfig;
using noted::domain::tool::ShapeKind;
using noted::stroke::BrushStyle;
using noted::stroke::Stroke;
using noted::stroke::StrokeSample;

[[nodiscard]] auto make_stroke(std::vector<StrokeSample> samples) -> Stroke {
    Stroke s;
    s.samples = std::move(samples);
    s.style = BrushStyle{};
    return s;
}

[[nodiscard]] auto circle_stroke(float cx, float cy, float r, int n) -> Stroke {
    std::vector<StrokeSample> samples;
    samples.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float t =
            (static_cast<float>(i) / static_cast<float>(n)) * 2.0F * std::numbers::pi_v<float>;
        samples.push_back({.x = cx + r * std::cos(t), .y = cy + r * std::sin(t), .pressure = 1.0F});
    }
    return make_stroke(std::move(samples));
}

[[nodiscard]] auto ellipse_stroke(float cx, float cy, float rx, float ry, int n) -> Stroke {
    std::vector<StrokeSample> samples;
    samples.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float t =
            (static_cast<float>(i) / static_cast<float>(n)) * 2.0F * std::numbers::pi_v<float>;
        samples.push_back(
            {.x = cx + rx * std::cos(t), .y = cy + ry * std::sin(t), .pressure = 1.0F});
    }
    return make_stroke(std::move(samples));
}

[[nodiscard]] auto rectangle_stroke(float x0, float y0, float x1, float y1, int samples_per_side)
    -> Stroke {
    std::vector<StrokeSample> samples;
    samples.reserve(static_cast<std::size_t>(samples_per_side * 4));
    auto edge = [&](float ax, float ay, float bx, float by) {
        for (int i = 0; i < samples_per_side; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(samples_per_side);
            samples.push_back({.x = ax + (bx - ax) * t, .y = ay + (by - ay) * t, .pressure = 1.0F});
        }
    };
    edge(x0, y0, x1, y0);  // top
    edge(x1, y0, x1, y1);  // right
    edge(x1, y1, x0, y1);  // bottom
    edge(x0, y1, x0, y0);  // left
    return make_stroke(std::move(samples));
}

}  // namespace

// ---- Trivial rejection paths -----------------------------------------------

TEST(ShapeDetect, EmptyStrokeReturnsNullopt) {
    EXPECT_FALSE(detect_shape(make_stroke({}), {}));
}

TEST(ShapeDetect, FewerThanMinSamplesReturnsNullopt) {
    Stroke s = make_stroke({
        {.x = 0.0F, .y = 0.0F},
        {.x = 1.0F, .y = 1.0F},
        {.x = 2.0F, .y = 2.0F},
    });
    DetectionConfig cfg{};
    cfg.min_samples = 8;
    EXPECT_FALSE(detect_shape(s, cfg));
}

TEST(ShapeDetect, NanSampleReturnsNullopt) {
    auto s = circle_stroke(100.0F, 100.0F, 50.0F, 32);
    s.samples[5].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(detect_shape(s, {}));
}

TEST(ShapeDetect, TinyShapeReturnsNullopt) {
    // 5-px diagonal is below default min_bbox_diagonal_px (16).
    DetectionConfig cfg{};
    auto s = circle_stroke(100.0F, 100.0F, 1.5F, 32);
    EXPECT_FALSE(detect_shape(s, cfg));
}

TEST(ShapeDetect, OpenCurveReturnsNullopt) {
    // Half-circle — gap from (cx+r, cy) to (cx-r, cy) is 2r ≈ bbox
    // diagonal → way over closure_ratio.
    std::vector<StrokeSample> samples;
    for (int i = 0; i <= 16; ++i) {
        const float t = (static_cast<float>(i) / 16.0F) * std::numbers::pi_v<float>;
        samples.push_back({.x = 100.0F + 50.0F * std::cos(t), .y = 100.0F + 50.0F * std::sin(t)});
    }
    EXPECT_FALSE(detect_shape(make_stroke(std::move(samples)), {}));
}

// ---- Ellipse detection -----------------------------------------------------

TEST(ShapeDetect, PerfectCircleDetectsAsEllipse) {
    auto s = circle_stroke(100.0F, 200.0F, 60.0F, 48);
    auto r = detect_ellipse(s, {});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->shape.kind, ShapeKind::ellipse);
    EXPECT_GT(r->confidence, 0.9F);
    // bbox is x ∈ [40, 160], y ∈ [140, 260].
    EXPECT_NEAR(r->shape.x0, 40.0, 1.0);
    EXPECT_NEAR(r->shape.x1, 160.0, 1.0);
    EXPECT_NEAR(r->shape.y0, 140.0, 1.0);
    EXPECT_NEAR(r->shape.y1, 260.0, 1.0);
}

TEST(ShapeDetect, FlatEllipseDetectsWithCorrectAspect) {
    // Wide ellipse: rx=80, ry=20. Aspect 4:1.
    auto s = ellipse_stroke(0.0F, 0.0F, 80.0F, 20.0F, 48);
    auto r = detect_ellipse(s, {});
    ASSERT_TRUE(r);
    EXPECT_GT(r->confidence, 0.85F);
    const double w = r->shape.x1 - r->shape.x0;
    const double h = r->shape.y1 - r->shape.y0;
    EXPECT_NEAR(w / h, 4.0, 0.2);
}

TEST(ShapeDetect, RectangleDoesNotMatchEllipse) {
    auto s = rectangle_stroke(0.0F, 0.0F, 200.0F, 100.0F, 12);
    // Tight tolerance — rectangle perimeter is far from the
    // inscribed ellipse.
    DetectionConfig cfg{};
    cfg.tolerance_px = 5.0F;
    EXPECT_FALSE(detect_ellipse(s, cfg));
}

// ---- Rectangle detection ---------------------------------------------------

TEST(ShapeDetect, AxisAlignedRectangleDetects) {
    auto s = rectangle_stroke(50.0F, 60.0F, 250.0F, 160.0F, 16);
    auto r = detect_rectangle(s, {});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->shape.kind, ShapeKind::rectangle);
    EXPECT_GT(r->confidence, 0.9F);
    EXPECT_NEAR(r->shape.x0, 50.0, 0.5);
    EXPECT_NEAR(r->shape.y0, 60.0, 0.5);
    EXPECT_NEAR(r->shape.x1, 250.0, 0.5);
    EXPECT_NEAR(r->shape.y1, 160.0, 0.5);
}

TEST(ShapeDetect, CircleDoesNotMatchRectangle) {
    auto s = circle_stroke(100.0F, 100.0F, 50.0F, 48);
    // Tight tolerance — a circle's perimeter is far from the
    // circumscribing rectangle's sides on the diagonals.
    DetectionConfig cfg{};
    cfg.tolerance_px = 5.0F;
    EXPECT_FALSE(detect_rectangle(s, cfg));
}

// ---- detect_shape arbitration ---------------------------------------------

TEST(ShapeDetect, BestMatchWins_CircleScoresHigherThanRectangleForCircleStroke) {
    auto s = circle_stroke(100.0F, 100.0F, 50.0F, 48);
    auto r = detect_shape(s, {});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->shape.kind, ShapeKind::ellipse);
}

TEST(ShapeDetect, BestMatchWins_RectangleScoresHigherThanEllipseForRectStroke) {
    auto s = rectangle_stroke(0.0F, 0.0F, 200.0F, 200.0F, 16);
    auto r = detect_shape(s, {});
    ASSERT_TRUE(r);
    EXPECT_EQ(r->shape.kind, ShapeKind::rectangle);
}

TEST(ShapeDetect, LowConfidenceBelowThresholdReturnsNullopt) {
    // Noisy partial circle — closes, but with > tolerance deviation.
    std::vector<StrokeSample> samples;
    for (int i = 0; i < 48; ++i) {
        const float t = (static_cast<float>(i) / 48.0F) * 2.0F * std::numbers::pi_v<float>;
        const float wobble = 25.0F * std::sin(t * 7.0F);  // big radial wobble
        const float r = 50.0F + wobble;
        samples.push_back({.x = 100.0F + r * std::cos(t), .y = 100.0F + r * std::sin(t)});
    }
    samples.push_back(samples.front());
    DetectionConfig cfg{};
    cfg.min_confidence = 0.55F;
    EXPECT_FALSE(detect_shape(make_stroke(std::move(samples)), cfg));
}

// ---- Brush style propagation ----------------------------------------------

TEST(ShapeDetect, RecognisedShapeInheritsBrushStyle) {
    auto s = circle_stroke(100.0F, 100.0F, 50.0F, 48);
    s.style.r = 0.8F;
    s.style.g = 0.2F;
    s.style.b = 0.1F;
    s.style.a = 0.7F;
    s.style.max_radius_px = 5.5F;
    auto r = detect_shape(s, {});
    ASSERT_TRUE(r);
    EXPECT_FLOAT_EQ(r->shape.stroke_r, 0.8F);
    EXPECT_FLOAT_EQ(r->shape.stroke_g, 0.2F);
    EXPECT_FLOAT_EQ(r->shape.stroke_b, 0.1F);
    EXPECT_FLOAT_EQ(r->shape.stroke_a, 0.7F);
    EXPECT_FLOAT_EQ(r->shape.stroke_width_px, 5.5F);
}
