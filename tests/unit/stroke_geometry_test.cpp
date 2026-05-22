#include "noted/engine/stroke/stroke_geometry.hpp"

#include <cmath>
#include <limits>
#include <unordered_set>

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

// ---- Per-segment TRIANGLE_LIST topology ------------------------------------
//
// Tessellator now emits 6 vertices per segment (2 triangles forming
// a rounded-cap quad), not 2 per sample. See `stroke_geometry.hpp`.

TEST(Tessellate, TwoSamplesProduceSixVertices) {
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
}

TEST(Tessellate, NSegmentsProduceSixNVertices) {
    Stroke s;
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.alpha_gamma = 1.0F;
    for (int i = 0; i < 50; ++i) {
        s.samples.push_back({.x = static_cast<float>(i), .y = 0.0F, .pressure = 1.0F});
    }
    const auto verts = tessellate_ribbon(s);
    // 49 segments × 6 vertices each = 294.
    EXPECT_EQ(verts.size(), 49U * 6U);
}

// ---- Quad geometry (single horizontal segment) -----------------------------
//
// Tangent (1, 0), radius 4. The segment's quad extends by radius along
// the tangent past each endpoint and ±radius along the perpendicular
// (0, 1). For samples A=(0,0), B=(10,0), r=4 the four corners are:
//   V0 (A - t*r - n*r) = (0 - 4, 0 - 4) = (-4, -4)
//   V1 (A - t*r + n*r) = (-4, +4)
//   V2 (B + t*r - n*r) = (14, -4)
//   V3 (B + t*r + n*r) = (14, +4)
// Triangle list emits these as (V0, V1, V2, V1, V3, V2).

TEST(Tessellate, HorizontalSegmentEmitsExtendedQuadCorners) {
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);

    // Triangle 1: V0, V1, V2 → (-4,-4), (-4,4), (14,-4)
    EXPECT_TRUE(near_f(verts[0].x, -4.0F));
    EXPECT_TRUE(near_f(verts[0].y, -4.0F));
    EXPECT_TRUE(near_f(verts[1].x, -4.0F));
    EXPECT_TRUE(near_f(verts[1].y, 4.0F));
    EXPECT_TRUE(near_f(verts[2].x, 14.0F));
    EXPECT_TRUE(near_f(verts[2].y, -4.0F));

    // Triangle 2: V1, V3, V2 → (-4,4), (14,4), (14,-4)
    EXPECT_TRUE(near_f(verts[3].x, -4.0F));
    EXPECT_TRUE(near_f(verts[3].y, 4.0F));
    EXPECT_TRUE(near_f(verts[4].x, 14.0F));
    EXPECT_TRUE(near_f(verts[4].y, 4.0F));
    EXPECT_TRUE(near_f(verts[5].x, 14.0F));
    EXPECT_TRUE(near_f(verts[5].y, -4.0F));
}

TEST(Tessellate, SdfCoordsAreAtQuadCorners) {
    // V0 / V2 are on the LEFT edge (side = -1); V1 / V3 are on the
    // RIGHT edge (side = +1). V0 / V1 are at the start-cap rim
    // (t = -1); V2 / V3 are at the end-cap rim (t = +1).
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    // Triangle 1: V0, V1, V2
    EXPECT_TRUE(near_f(verts[0].side, -1.0F));
    EXPECT_TRUE(near_f(verts[0].t, -1.0F));
    EXPECT_TRUE(near_f(verts[1].side, 1.0F));
    EXPECT_TRUE(near_f(verts[1].t, -1.0F));
    EXPECT_TRUE(near_f(verts[2].side, -1.0F));
    EXPECT_TRUE(near_f(verts[2].t, 1.0F));
}

TEST(Tessellate, KEncodesBodyHalfLengthOverRadius) {
    // Segment length 10, radius 4 → K = 5/4 = 1.25.
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    for (const auto& v : verts) {
        EXPECT_TRUE(near_f(v.K, 1.25F, 0.001F)) << "K should be constant within a segment";
    }
}

// ---- Multiple segments -----------------------------------------------------

TEST(Tessellate, ThreeSamplesProduceTwoSegmentQuads) {
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    // 2 segments × 6 vertices = 12.
    ASSERT_EQ(verts.size(), 12U);
}

// ---- U-turn robustness (the bug per-segment topology was built to fix) -----

TEST(Tessellate, UTurnZigzagDoesNotCollapseAnyVertex) {
    // The old per-sample triangle-strip tessellator pinched the
    // ribbon at sharp direction reversals because the average-
    // tangent perpendicular flipped between adjacent samples. The
    // per-segment scheme is robust by construction: each segment
    // is independent, so no vertex collapses no matter how sharp
    // the turn.
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},   // 180° U-turn
        {.x = 10.0F, .y = 0.0F, .pressure = 1.0F},  // and back again
    });
    const auto verts = tessellate_ribbon(s);
    // 3 segments × 6 = 18 (coalesce keeps the U-turn samples since
    // their (x, y) differs between consecutive entries even though
    // their values coincide with earlier samples).
    ASSERT_EQ(verts.size(), 18U);

    // No two adjacent vertices in the same triangle should share
    // a position — that would mean a zero-area triangle, the
    // signature of the old pinch bug.
    for (std::size_t tri = 0; tri < verts.size(); tri += 3U) {
        const auto& v0 = verts[tri];
        const auto& v1 = verts[tri + 1U];
        const auto& v2 = verts[tri + 2U];
        EXPECT_FALSE(near_f(v0.x, v1.x) && near_f(v0.y, v1.y)) << "tri=" << tri;
        EXPECT_FALSE(near_f(v1.x, v2.x) && near_f(v1.y, v2.y)) << "tri=" << tri;
        EXPECT_FALSE(near_f(v0.x, v2.x) && near_f(v0.y, v2.y)) << "tri=" << tri;
    }
}

// ---- Right-angle turn -----------------------------------------------------

TEST(Tessellate, RightAngleTurnEmitsTwoIndependentSegments) {
    // Each segment's quad is computed from its own sample pair, so
    // the right turn at the middle sample produces two segment
    // quads that share the corner point (5, 0) at the inner end of
    // segment 1 and the outer start of segment 2. The fragment
    // shader's capsule SDF rounds the join naturally where the two
    // capsules overlap.
    auto s = fixed_width_stroke({
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 0.0F, .pressure = 1.0F},
        {.x = 5.0F, .y = 5.0F, .pressure = 1.0F},
    });
    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 12U);

    // First segment: horizontal, K = 2.5/4 = 0.625
    EXPECT_TRUE(near_f(verts[0].K, 0.625F, 0.001F));
    // Second segment: vertical of same length, same K.
    EXPECT_TRUE(near_f(verts[6].K, 0.625F, 0.001F));
}

// ---- Colour propagation ---------------------------------------------------

TEST(Tessellate, VelocityBlendZeroLeavesRadiusUntouched) {
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F},
        {.x = 1500.0F, .y = 0.0F, .pressure = 1.0F, .t = 1.0F},  // 1500 px/s
    };
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.velocity_blend = 0.0F;  // velocity ignored

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    // With velocity_blend = 0, the segment radius equals the
    // pressure-mapped radius (4 px). The capsule occupies y ∈
    // [-4, 4] perpendicular to the stroke; verify the y extent.
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    EXPECT_TRUE(near_f(max_y - min_y, 8.0F, 0.5F));
}

TEST(Tessellate, VelocityBlendFullShrinksFastStroke) {
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F},
        {.x = 1500.0F, .y = 0.0F, .pressure = 1.0F, .t = 1.0F},  // exactly reference velocity
    };
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.velocity_blend = 1.0F;

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    // With blend=1 and v == reference velocity, dampening factor
    // is 1 - 1*1*0.7 = 0.3. Expected ribbon thickness ≈ 8 * 0.3
    // = 2.4 px ± rounding. The tessellator's 0.5 px floor on
    // radius keeps it from collapsing.
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    EXPECT_LT(max_y - min_y, 5.0F);  // significantly thinner than 8 px
}

TEST(Tessellate, ZeroTimestampSamplesSkipVelocityDampening) {
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F},
        {.x = 100.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F},  // dt = 0
    };
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.velocity_blend = 1.0F;  // would normally dampen

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    // dt == 0 → tessellator skips the velocity branch; ribbon
    // renders at full pressure radius even with blend=1. This is
    // the legacy-stroke path (loaded files with no per-sample t).
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    EXPECT_TRUE(near_f(max_y - min_y, 8.0F, 0.5F));
}

TEST(Tessellate, TiltBlendZeroIgnoresTilt) {
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F, .tilt_x = 60.0F, .tilt_y = 0.0F},
        {.x = 100.0F, .y = 0.0F, .pressure = 1.0F, .t = 1.0F, .tilt_x = 60.0F, .tilt_y = 0.0F},
    };
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.tilt_blend = 0.0F;

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    // Full pressure radius (4) → ribbon thickness ~8 px ignoring tilt.
    EXPECT_TRUE(near_f(max_y - min_y, 8.0F, 0.5F));
}

TEST(Tessellate, TiltParallelToStrokeThins) {
    // Stroke runs along +x, pen tilt also along +x (60° tilt_x).
    // With tilt_blend = 1 the segment should attenuate to floor.
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F, .tilt_x = 70.0F, .tilt_y = 0.0F},
        {.x = 100.0F, .y = 0.0F, .pressure = 1.0F, .t = 1.0F, .tilt_x = 70.0F, .tilt_y = 0.0F},
    };
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.tilt_blend = 1.0F;

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    // Significantly thinner than 8 px — calligraphy chisel-drag.
    EXPECT_LT(max_y - min_y, 6.5F);
}

TEST(Tessellate, TiltPerpendicularToStrokeKeepsFullWidth) {
    // Stroke +x, tilt +y → perpendicular → broad side of chisel.
    Stroke s;
    s.samples = {
        {.x = 0.0F, .y = 0.0F, .pressure = 1.0F, .t = 0.0F, .tilt_x = 0.0F, .tilt_y = 70.0F},
        {.x = 100.0F, .y = 0.0F, .pressure = 1.0F, .t = 1.0F, .tilt_x = 0.0F, .tilt_y = 70.0F},
    };
    s.style.min_radius_px = 4.0F;
    s.style.max_radius_px = 4.0F;
    s.style.tilt_blend = 1.0F;

    const auto verts = tessellate_ribbon(s);
    ASSERT_EQ(verts.size(), 6U);
    float min_y = std::numeric_limits<float>::infinity();
    float max_y = -std::numeric_limits<float>::infinity();
    for (const auto& v : verts) {
        min_y = std::min(min_y, v.y);
        max_y = std::max(max_y, v.y);
    }
    // Tilt perpendicular to stroke direction → no thinning.
    EXPECT_TRUE(near_f(max_y - min_y, 8.0F, 0.5F));
}

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
    ASSERT_EQ(verts.size(), 6U);
    for (const auto& v : verts) {
        EXPECT_TRUE(near_f(v.r, 0.8F, 0.05F));
        EXPECT_TRUE(near_f(v.g, 0.2F, 0.05F));
        EXPECT_TRUE(near_f(v.b, 0.1F, 0.05F));
        // alpha is multiplied by the pressure curve; pressure=1 +
        // gamma=1 → unchanged.
        EXPECT_TRUE(near_f(v.a, 1.0F, 0.01F));
    }
}
