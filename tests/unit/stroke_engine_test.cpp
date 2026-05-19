#include "noted/engine/stroke/stroke_engine.hpp"

#include <cmath>

#include <gtest/gtest.h>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/stroke/stroke_geometry.hpp"

namespace {

using PB = noted::hook::PointerButton;
using StrokeEngine = noted::stroke::StrokeEngine;
using BrushStyle = noted::stroke::BrushStyle;
using noted::stroke::stamp_from_pressure;

// Build an engine that has no pipeline / no subscriptions — pure
// event processing. Pointer events are fed through inject_*
// helpers which invoke the same on_* methods the hook
// subscriptions would.
auto make_test_engine() {
    return StrokeEngine{StrokeEngine::TestingTag{}};
}

}  // namespace

// ---- Event semantics: press / move / release ------------------------------

TEST(StrokeEngine, IgnoresMoveWithoutPress) {
    auto eng = make_test_engine();
    eng.inject_move_(100.0, 100.0);
    eng.inject_move_(110.0, 105.0);
    EXPECT_EQ(eng.total_sample_count(), 0U);
    EXPECT_EQ(eng.stroke_count(), 0U);
    EXPECT_FALSE(eng.is_drawing());
}

TEST(StrokeEngine, PressMoveAccumulatesIntoCurrentStroke) {
    auto eng = make_test_engine();
    eng.inject_press_(10.0, 20.0, PB::left);
    EXPECT_TRUE(eng.is_drawing());
    eng.inject_move_(11.0, 21.0);
    eng.inject_move_(12.0, 22.0);

    // Mid-stroke: nothing committed yet, samples live in the
    // in-flight stroke.
    EXPECT_EQ(eng.stroke_count(), 0U);
    EXPECT_EQ(eng.current_stroke().samples.size(), 3U);
    EXPECT_FLOAT_EQ(eng.current_stroke().samples[0].x, 10.0F);
    EXPECT_FLOAT_EQ(eng.current_stroke().samples[1].x, 11.0F);
    EXPECT_FLOAT_EQ(eng.current_stroke().samples[2].x, 12.0F);
}

TEST(StrokeEngine, ReleaseCommitsStrokeToCompletedList) {
    auto eng = make_test_engine();
    eng.inject_press_(10.0, 20.0, PB::left);
    eng.inject_move_(11.0, 21.0);
    eng.inject_release_(11.0, 21.0, PB::left);
    EXPECT_FALSE(eng.is_drawing());

    ASSERT_EQ(eng.stroke_count(), 1U);
    // current_stroke_ is reset after a commit.
    EXPECT_EQ(eng.current_stroke().samples.size(), 0U);
    ASSERT_EQ(eng.strokes()[0].samples.size(), 2U);
    EXPECT_FLOAT_EQ(eng.strokes()[0].samples[0].x, 10.0F);
    EXPECT_FLOAT_EQ(eng.strokes()[0].samples[1].x, 11.0F);
}

TEST(StrokeEngine, SingleSamplePressReleaseIsDroppedNotCommitted) {
    // A press followed by an immediate release with no movement
    // has nothing the ribbon tessellator can render. Storing it
    // would just be noise — the engine drops it on release.
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_release_(0.0, 0.0, PB::left);
    EXPECT_EQ(eng.stroke_count(), 0U);
    EXPECT_EQ(eng.total_sample_count(), 0U);
}

TEST(StrokeEngine, MovesAfterReleaseAreIgnored) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::left);
    eng.inject_move_(5.0, 5.0);  // mouse moves with no button — drop
    eng.inject_move_(6.0, 6.0);
    ASSERT_EQ(eng.stroke_count(), 1U);
    EXPECT_EQ(eng.strokes()[0].samples.size(), 2U);  // press + first move only
}

TEST(StrokeEngine, RightButtonDoesNotDraw) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::right);
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::right);
    EXPECT_EQ(eng.total_sample_count(), 0U);
    EXPECT_EQ(eng.stroke_count(), 0U);
    EXPECT_FALSE(eng.is_drawing());
}

TEST(StrokeEngine, OverlappingButtonsLeftOwnsTheStroke) {
    // Reasonable behaviour: left starts a stroke, a stray right
    // press does not end it; a left release does. Mirrors how
    // most paint tools behave.
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_press_(0.0, 0.0, PB::right);  // shouldn't toggle drawing_
    EXPECT_TRUE(eng.is_drawing());
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::right);  // wrong button — no-op
    EXPECT_TRUE(eng.is_drawing());
    eng.inject_release_(1.0, 1.0, PB::left);
    EXPECT_FALSE(eng.is_drawing());
    EXPECT_EQ(eng.stroke_count(), 1U);
}

TEST(StrokeEngine, MultipleStrokesAccumulate) {
    auto eng = make_test_engine();
    for (int i = 0; i < 3; ++i) {
        eng.inject_press_(static_cast<double>(i * 10), 0.0, PB::left);
        eng.inject_move_(static_cast<double>(i * 10 + 1), 0.0);
        eng.inject_release_(static_cast<double>(i * 10 + 1), 0.0, PB::left);
    }
    EXPECT_EQ(eng.stroke_count(), 3U);
    EXPECT_EQ(eng.total_sample_count(), 6U);
}

TEST(StrokeEngine, ClearStrokesResetsBothLists) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::left);
    eng.inject_press_(2.0, 2.0, PB::left);  // in-flight stroke
    EXPECT_EQ(eng.stroke_count(), 1U);
    EXPECT_EQ(eng.current_stroke().samples.size(), 1U);

    eng.clear_strokes();
    EXPECT_EQ(eng.stroke_count(), 0U);
    EXPECT_EQ(eng.current_stroke().samples.size(), 0U);
    EXPECT_EQ(eng.total_sample_count(), 0U);
    // is_drawing() reflects pointer phase, not buffer state.
    EXPECT_TRUE(eng.is_drawing());
}

TEST(StrokeEngine, ResizeUpdatesCanvasSize) {
    auto eng = make_test_engine();
    eng.inject_resize_(800U, 600U);
    // No public getter for canvas_w_/h_ — but if record() were
    // callable without a pipeline it'd use these. Smoke test that
    // the event handler runs without UB / crash.
    SUCCEED();
}

// ---- Brush style snapshot per stroke -------------------------------------

TEST(StrokeEngine, StrokeStyleIsSnapshotAtPressTime) {
    StrokeEngine eng{StrokeEngine::TestingTag{}};
    BrushStyle s{};
    s.min_radius_px = 3.0F;
    s.max_radius_px = 9.0F;
    s.r = 0.5F;
    s.g = 0.25F;
    s.b = 0.0F;
    eng.set_brush(s);
    eng.inject_press_(0.0, 0.0, PB::left, 0.5F);
    eng.inject_move_(1.0, 0.0, 0.5F);
    eng.inject_release_(1.0, 0.0, PB::left);

    // Mutate the brush AFTER the stroke is committed — the stored
    // style should retain the values captured at press time.
    BrushStyle different{};
    different.min_radius_px = 100.0F;
    different.r = 1.0F;
    eng.set_brush(different);

    ASSERT_EQ(eng.stroke_count(), 1U);
    EXPECT_FLOAT_EQ(eng.strokes()[0].style.min_radius_px, 3.0F);
    EXPECT_FLOAT_EQ(eng.strokes()[0].style.max_radius_px, 9.0F);
    EXPECT_FLOAT_EQ(eng.strokes()[0].style.r, 0.5F);
}

TEST(StrokeEngine, SamplesCarryPerEventPressure) {
    StrokeEngine eng{StrokeEngine::TestingTag{}};
    eng.inject_press_(0.0, 0.0, PB::left, /*pressure=*/0.25F);
    eng.inject_move_(1.0, 1.0, /*pressure=*/0.5F);
    eng.inject_move_(2.0, 2.0, /*pressure=*/0.75F);
    eng.inject_release_(2.0, 2.0, PB::left);

    ASSERT_EQ(eng.stroke_count(), 1U);
    const auto& s = eng.strokes()[0].samples;
    ASSERT_EQ(s.size(), 3U);
    EXPECT_FLOAT_EQ(s[0].pressure, 0.25F);
    EXPECT_FLOAT_EQ(s[1].pressure, 0.5F);
    EXPECT_FLOAT_EQ(s[2].pressure, 0.75F);
}

// ---- stamp_from_pressure: pure mapping coverage --------------------------
//
// The pressure curve is the single source of truth shared between
// the (deprecated) per-sample stamp path and the new ribbon
// tessellator. Tests below stay valid across the rendering swap.

TEST(StrokeEnginePressure, RadiusLerpsLinearly) {
    BrushStyle style{};
    style.min_radius_px = 2.0F;
    style.max_radius_px = 10.0F;
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 0.0F).radius_px, 2.0F);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 1.0F).radius_px, 10.0F);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 0.5F).radius_px, 6.0F);
}

TEST(StrokeEnginePressure, AlphaUsesGammaCurve) {
    BrushStyle style{};
    style.a = 1.0F;
    style.alpha_gamma = 2.0F;
    // pressure = 0.5, gamma = 2 → alpha = 0.5^2 = 0.25.
    EXPECT_NEAR(stamp_from_pressure(style, 0.5F).a, 0.25F, 1e-5F);
}

TEST(StrokeEnginePressure, AlphaGammaOneIsLinear) {
    BrushStyle style{};
    style.alpha_gamma = 1.0F;
    EXPECT_NEAR(stamp_from_pressure(style, 0.7F).a, 0.7F, 1e-5F);
}

TEST(StrokeEnginePressure, SoftnessTracksRadiusWithFloor) {
    BrushStyle style{};
    style.min_radius_px = 0.5F;
    style.max_radius_px = 50.0F;
    style.softness_ratio = 0.20F;
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 0.0F).softness_px, 1.0F);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 1.0F).softness_px, 10.0F);
}

TEST(StrokeEnginePressure, PressureClampedToZeroOne) {
    BrushStyle style{};
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, -0.5F).radius_px, style.min_radius_px);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 2.0F).radius_px, style.max_radius_px);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, std::nanf("")).radius_px, style.min_radius_px);
}

TEST(StrokeEnginePressure, ColorPassesThrough) {
    BrushStyle style{};
    style.r = 0.7F;
    style.g = 0.3F;
    style.b = 0.1F;
    style.a = 1.0F;
    const auto s = stamp_from_pressure(style, 0.8F);
    EXPECT_FLOAT_EQ(s.r, 0.7F);
    EXPECT_FLOAT_EQ(s.g, 0.3F);
    EXPECT_FLOAT_EQ(s.b, 0.1F);
}

TEST(StrokeEnginePressure, SetBrushMutatesLiveStyle) {
    // The live `brush_` is the snapshot source for the NEXT
    // press. Mutating it does not change accumulated strokes.
    StrokeEngine eng{StrokeEngine::TestingTag{}};
    BrushStyle s{};
    s.min_radius_px = 5.0F;
    s.max_radius_px = 5.0F;
    eng.set_brush(s);
    EXPECT_FLOAT_EQ(eng.brush().min_radius_px, 5.0F);
    EXPECT_FLOAT_EQ(eng.brush().max_radius_px, 5.0F);
}
