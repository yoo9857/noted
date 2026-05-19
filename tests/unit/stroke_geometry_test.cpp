#include "noted/engine/stroke/stroke_geometry.hpp"

#include <cmath>

#include <gtest/gtest.h>

namespace {

using noted::stroke::BrushStyle;
using noted::stroke::RibbonVertex;
using noted::stroke::Stroke;
using noted::stroke::StrokeSample;
using noted::stroke::tessellate_ribbon;

[[nodiscard]] auto near_f(float a, float b, float eps = 1e-4F) -> bool {
    return std::abs(a - b) <= eps;
}

// Build a stroke with a fixed-width brush so the tessellator's
// per-sample width is deterministic.
[[nodiscard]] auto fixed_width_stroke(std::initializer_list<StrokeSample> samples) -> Stroke {
    Stroke s;
    s.samples.assign(samples.begin(), samples.end());
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.alpha_gamma = 1.0F;
    s.style.softness_ratio = 0.0F;
    s.style.r = 0.0F;
    s.style.g = 0.0F;
    s.style.b = 0.0F;
    s.style.a = 1.0F;
    return s;
}

}  // namespace

// ---- Degenerate inputs ----------------------------------------------------

TEST(Tessellate, EmptyStrokeYieldsEmpty) {
    Stroke s;
    EXPECT_TRUE(tessellate_ribbon(s).empty());
}

TEST(Tessellate, SingleSampleYieldsEmpty) {
    // A lone pen down with no movement has no segments; nothing
    // useful to render with a triangle strip. (Dot rendering is a
    // separate concern — special-cased by the renderer if needed.)
    auto s = fixed_width_stroke({{.x = 10.0F, .y = 20.0F, .pressure = 1.0F}});
    EXPECT_TRUE(tessellate_ribbon(s).empty());
}

TEST(Tessellate, ConsecutiveDuplicateSamplesCollapse) {
    // Three identical samples = one effective sample after
    // coalescing = empty ribbon.
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
    });
    EXPECT_TRUE(tessellate_ribbon(s).empty());
}

// ---- Two-sample straight segment ------------------------------------------

TEST(Tessellate, TwoSamplesProduceFourVertices) {
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 4U);
}

TEST(Tessellate, TwoSamplesNormalsArePerpendicularToTangent) {
    // Horizontal segment (1, 0). The unit perpendicular in screen
    // space (y-down) is (0, 1) — so the "left" vertex is sample +
    // (0, -1)*w and "right" is sample + (0, 1)*w when nx/ny are
    // taken as (-dy/len, dx/len). With dx=10, dy=0 → nx=0, ny=1.
    // left  = (x, y) - (0, 1) * 4 = (x, y - 4)
    // right = (x, y) + (0, 1) * 4 = (x, y + 4)
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 4U);

    // Sample 0: left at (0, -4), right at (0, 4)
    EXPECT_TRUE(near_f(verts[0].x, 0.0F));
    EXPECT_TRUE(near_f(verts[0].y, -4.0F));
    EXPECT_TRUE(near_f(verts[1].x, 0.0F));
    EXPECT_TRUE(near_f(verts[1].y, 4.0F));

    // Sample 1: left at (10, -4), right at (10, 4)
    EXPECT_TRUE(near_f(verts[2].x, 10.0F));
    EXPECT_TRUE(near_f(verts[2].y, -4.0F));
    EXPECT_TRUE(near_f(verts[3].x, 10.0F));
    EXPECT_TRUE(near_f(verts[3].y, 4.0F));
}

// ---- Three-sample collinear stroke ----------------------------------------

TEST(Tessellate, CollinearThreeSamplesProduceSixVertices) {
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);

    // Middle sample's tangent is the average of two horizontal
    // segments → still horizontal → normal still (0, 1).
    EXPECT_TRUE(near_f(verts[2].x, 5.0F));
    EXPECT_TRUE(near_f(verts[2].y, -4.0F));
    EXPECT_TRUE(near_f(verts[3].x, 5.0F));
    EXPECT_TRUE(near_f(verts[3].y, 4.0F));
}

// ---- Pressure-modulated width --------------------------------------------

TEST(Tessellate, PressureRampWidensRibbon) {
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 0.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 0.5F},
        {.x = 20.0F, .y = 0.0F, .pressure = 1.0F},
    };
    // Brush with min=2, max=10 — pressure 0 → r=2, pressure 1 →
    // r=10 modulo the gamma curve.
    s.style.min_radius_px = 2.0F;
    s.style.max_radius_px = 10.0F;
    s.style.alpha_gamma = 1.0F;

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);

    const float w0 = std::abs(verts[1].y - verts[0].y) * 0.5F;
    const float w1 = std::abs(verts[3].y - verts[2].y) * 0.5F;
    const float w2 = std::abs(verts[5].y - verts[4].y) * 0.5F;

    EXPECT_LT(w0, w1);
    EXPECT_LT(w1, w2);
    // First sample's pressure 0 → minimum radius.
    EXPECT_TRUE(near_f(w0, 2.0F, 0.1F));
    // Last sample's pressure 1 → maximum radius.
    EXPECT_TRUE(near_f(w2, 10.0F, 0.1F));
}

// ---- Non-axis-aligned turn -----------------------------------------------

TEST(Tessellate, RightAngleTurnAveragesIncomingOutgoing) {
    // Right turn: horizontal segment, then vertical segment. The
    // middle sample's tangent averages (1,0) and (0,1) → (0.5,0.5).
    // Normal = (-0.5,0.5) normalized → (-√2/2, √2/2). Half-width
    // is 4, so:
    //   left  = (5,0) - 4*(-√2/2, √2/2) = (5 + 2√2, -2√2)
    //   right = (5,0) + 4*(-√2/2, √2/2) = (5 - 2√2,  2√2)
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 5.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);

    const float root2_half = std::sqrt(2.0F) * 0.5F;
    const float expected_offset = 4.0F * root2_half;  // 2√2 ≈ 2.828
    EXPECT_TRUE(near_f(verts[2].x, 5.0F + expected_offset, 0.01F));
    EXPECT_TRUE(near_f(verts[2].y, 0.0F - expected_offset, 0.01F));
    EXPECT_TRUE(near_f(verts[3].x, 5.0F - expected_offset, 0.01F));
    EXPECT_TRUE(near_f(verts[3].y, 0.0F + expected_offset, 0.01F));
}

// ---- Colour propagation ---------------------------------------------------

TEST(Tessellate, EveryVertexCarriesBrushColor) {
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    };
    s.style.r = 0.8F;
    s.style.g = 0.2F;
    s.style.b = 0.1F;
    s.style.a = 1.0F;
    s.style.alpha_gamma = 1.0F;
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 4U);
    for (const auto& v : verts) {
        EXPECT_TRUE(near_f(v.r, 0.8F, 0.05F));
        EXPECT_TRUE(near_f(v.g, 0.2F, 0.05F));
        EXPECT_TRUE(near_f(v.b, 0.1F, 0.05F));
        // alpha is multiplied by the pressure curve; pressure=1 +
        // gamma=1 → unchanged.
        EXPECT_TRUE(near_f(v.a, 1.0F, 0.01F));
    }
}

// ---- Vertex count grows linearly -------------------------------------------

TEST(Tessellate, VertexCountIsTwicePerCoalescedSample) {
    Stroke s;
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.alpha_gamma = 1.0F;
    for (int i = 0; i < 50; ++i) {
        s.samples.push_back({.x = static_cast<float>(i), .y = 0.0F, .pressure = 1.0F});
    }
    const auto verts = tessellate_ribbon(s);
    EXPECT_EQ(verts.size(), 100U);
}
