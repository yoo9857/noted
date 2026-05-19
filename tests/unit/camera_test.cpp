#include "noted/engine/canvas/camera.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace {

using noted::canvas::Camera;

[[nodiscard]] auto near_double(double a, double b, double eps = 1e-9) -> bool {
    return std::abs(a - b) <= eps;
}

}  // namespace

// ---- Identity transform ----------------------------------------------------

TEST(Camera, IdentityProjectIsAdditiveTranslation) {
    Camera c;
    c.set_canvas_extent(1000, 800);
    c.set_window_extent(1000, 800);
    EXPECT_EQ(c.project_x(0.0), 0.0);
    EXPECT_EQ(c.project_y(0.0), 0.0);
    EXPECT_EQ(c.project_x(123.5), 123.5);
    EXPECT_EQ(c.unproject_x(123.5), 123.5);
}

TEST(Camera, IdentityShaderHelpersMatchOriginalFullscreenTriangle) {
    // With scale=1, translation=0, canvas==window — vs_main needs
    // scale=2, translation=-1 to reproduce the existing
    // uv*2-1 → ndc mapping.
    Camera c;
    c.set_canvas_extent(1600, 1000);
    c.set_window_extent(1600, 1000);
    EXPECT_FLOAT_EQ(c.shader_scale_x(), 2.0F);
    EXPECT_FLOAT_EQ(c.shader_scale_y(), 2.0F);
    EXPECT_FLOAT_EQ(c.shader_translation_x(), -1.0F);
    EXPECT_FLOAT_EQ(c.shader_translation_y(), -1.0F);
}

// ---- Project / unproject round-trip ---------------------------------------

TEST(Camera, ProjectUnprojectIsInverse) {
    Camera c;
    c.set_canvas_extent(1000, 800);
    c.set_window_extent(1000, 800);
    c.set_scale(2.5);
    c.set_translation(40.0, -20.0);
    for (const double v : {0.0, 1.0, 50.0, 123.456, -10.0, 999.0}) {
        EXPECT_TRUE(near_double(c.unproject_x(c.project_x(v)), v));
        EXPECT_TRUE(near_double(c.unproject_y(c.project_y(v)), v));
    }
}

// ---- Zoom-around invariant ------------------------------------------------

TEST(Camera, ZoomAroundPinsAnchorCanvasPoint) {
    Camera c;
    c.set_canvas_extent(1000, 800);
    c.set_window_extent(1000, 800);
    c.set_scale(1.0);
    c.set_translation(0.0, 0.0);

    constexpr double kAnchorX = 400.0;
    constexpr double kAnchorY = 250.0;
    const double canvas_x_before = c.unproject_x(kAnchorX);
    const double canvas_y_before = c.unproject_y(kAnchorY);

    c.zoom_around(kAnchorX, kAnchorY, 1.5);

    // The canvas point that was under the anchor is still under it.
    EXPECT_TRUE(near_double(c.unproject_x(kAnchorX), canvas_x_before, 1e-9));
    EXPECT_TRUE(near_double(c.unproject_y(kAnchorY), canvas_y_before, 1e-9));
    EXPECT_TRUE(near_double(c.scale(), 1.5));
}

TEST(Camera, ZoomAroundSurvivesTranslatedInitialState) {
    // Repeat the invariant after panning so the test exercises a
    // non-zero starting translation.
    Camera c;
    c.set_canvas_extent(1000, 800);
    c.set_window_extent(1000, 800);
    c.set_scale(2.0);
    c.set_translation(-50.0, 30.0);

    constexpr double kAnchorX = 200.0;
    constexpr double kAnchorY = 100.0;
    const double cx = c.unproject_x(kAnchorX);
    const double cy = c.unproject_y(kAnchorY);

    c.zoom_around(kAnchorX, kAnchorY, 0.7);

    EXPECT_TRUE(near_double(c.unproject_x(kAnchorX), cx, 1e-9));
    EXPECT_TRUE(near_double(c.unproject_y(kAnchorY), cy, 1e-9));
}

TEST(Camera, ZoomAroundIgnoresPathologicalFactors) {
    // NaN / inf / zero / negative `factor` must not corrupt state.
    Camera c;
    c.set_canvas_extent(1000, 800);
    c.set_window_extent(1000, 800);
    c.set_scale(1.0);
    c.set_translation(0.0, 0.0);

    const double prior_scale = c.scale();
    c.zoom_around(500.0, 400.0, std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(c.scale(), prior_scale);

    c.zoom_around(500.0, 400.0, 0.0);
    EXPECT_DOUBLE_EQ(c.scale(), prior_scale);

    c.zoom_around(500.0, 400.0, -2.0);
    EXPECT_DOUBLE_EQ(c.scale(), prior_scale);

    c.zoom_around(500.0, 400.0, std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(c.scale(), prior_scale);
}

TEST(Camera, ZoomAroundClampsAgainstInternalCeiling) {
    Camera c;
    c.set_canvas_extent(1000, 800);
    c.set_window_extent(1000, 800);
    c.set_scale(1.0);

    // Multiply by 1e20 — internal ceiling at 1e+6 must absorb this.
    c.zoom_around(0.0, 0.0, 1e20);
    EXPECT_LE(c.scale(), 1e6 + 1.0);
}

// ---- Setters' input validation --------------------------------------------

TEST(Camera, SetScaleRejectsNonPositiveAndNonFinite) {
    Camera c;
    c.set_scale(3.0);
    const double prior = c.scale();

    c.set_scale(0.0);
    EXPECT_DOUBLE_EQ(c.scale(), prior);

    c.set_scale(-1.0);
    EXPECT_DOUBLE_EQ(c.scale(), prior);

    c.set_scale(std::numeric_limits<double>::infinity());
    EXPECT_DOUBLE_EQ(c.scale(), prior);

    c.set_scale(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(c.scale(), prior);

    c.set_scale(2.5);
    EXPECT_DOUBLE_EQ(c.scale(), 2.5);
}

TEST(Camera, SetExtentTreatsZeroAsOnePixel) {
    Camera c;
    c.set_canvas_extent(0, 0);
    c.set_window_extent(0, 0);
    // Should not crash or divide-by-zero anywhere downstream.
    EXPECT_GT(c.canvas_extent_w(), 0.0);
    EXPECT_GT(c.canvas_extent_h(), 0.0);
    EXPECT_GT(c.window_extent_w(), 0.0);
    EXPECT_GT(c.window_extent_h(), 0.0);
    (void) c.shader_scale_x();  // would NaN/inf if extent were 0
    (void) c.shader_translation_y();
}

// ---- Clamping --------------------------------------------------------------

TEST(Camera, ClampScaleHonorsRange) {
    Camera c;
    c.set_scale(0.05);
    c.clamp_scale(0.25, 4.0);
    EXPECT_DOUBLE_EQ(c.scale(), 0.25);

    c.set_scale(50.0);
    c.clamp_scale(0.25, 4.0);
    EXPECT_DOUBLE_EQ(c.scale(), 4.0);

    c.set_scale(1.0);
    c.clamp_scale(0.25, 4.0);
    EXPECT_DOUBLE_EQ(c.scale(), 1.0);
}

TEST(Camera, ClampScaleAcceptsCrossedBounds) {
    Camera c;
    c.set_scale(2.0);
    // Caller swaps min/max — the clamp should still pin to the
    // implied range.
    c.clamp_scale(10.0, 0.5);
    EXPECT_DOUBLE_EQ(c.scale(), 2.0);  // 2 is inside [0.5, 10]
    c.set_scale(0.1);
    c.clamp_scale(10.0, 0.5);
    EXPECT_DOUBLE_EQ(c.scale(), 0.5);
}

// ---- Translate by ----------------------------------------------------------

TEST(Camera, TranslateByIsAdditive) {
    Camera c;
    c.set_translation(10.0, 20.0);
    c.translate_by(5.0, -3.0);
    EXPECT_DOUBLE_EQ(c.translation_x(), 15.0);
    EXPECT_DOUBLE_EQ(c.translation_y(), 17.0);
}

// ---- Shader helpers under non-identity transform ---------------------------

TEST(Camera, ShaderTranslationOfMinusOneAtIdentityRecoveredAfterRoundTrip) {
    Camera c;
    c.set_canvas_extent(1600, 1000);
    c.set_window_extent(1600, 1000);
    c.set_scale(1.5);
    c.set_translation(200.0, -50.0);

    // shader_translation = 2 * t / W - 1
    EXPECT_FLOAT_EQ(c.shader_translation_x(), static_cast<float>(2.0 * 200.0 / 1600.0 - 1.0));
    EXPECT_FLOAT_EQ(c.shader_translation_y(), static_cast<float>(2.0 * -50.0 / 1000.0 - 1.0));
    // shader_scale = 2 * scale * canvas / window
    EXPECT_FLOAT_EQ(c.shader_scale_x(), static_cast<float>(2.0 * 1.5 * 1600.0 / 1600.0));
}
