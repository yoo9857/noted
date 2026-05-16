#include <gtest/gtest.h>

#include <cmath>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/stroke/stroke_engine.hpp"

namespace {

using PB = noted::hook::PointerButton;
using StrokeEngine = noted::stroke::StrokeEngine;
using BrushStyle   = noted::stroke::BrushStyle;
using noted::stroke::stamp_from_pressure;

// Build an engine that has no pipeline / no subscriptions — pure event
// processing. Pointer events are fed through inject_* helpers which
// invoke the same on_* methods the hook subscriptions would.
auto make_test_engine() {
    return StrokeEngine{StrokeEngine::TestingTag{}};
}

}  // namespace

TEST(StrokeEngine, IgnoresMoveWithoutPress) {
    auto eng = make_test_engine();
    eng.inject_move_(100.0, 100.0);
    eng.inject_move_(110.0, 105.0);
    EXPECT_EQ(eng.stamp_count(), 0U);
    EXPECT_FALSE(eng.is_drawing());
}

TEST(StrokeEngine, PressMoveReleaseAccumulatesStamps) {
    auto eng = make_test_engine();
    eng.inject_press_(10.0, 20.0, PB::left);
    EXPECT_TRUE(eng.is_drawing());
    eng.inject_move_(11.0, 21.0);
    eng.inject_move_(12.0, 22.0);
    eng.inject_release_(12.0, 22.0, PB::left);
    EXPECT_FALSE(eng.is_drawing());

    // press + 2 moves = 3 stamps. release does NOT add one (mirrors
    // typical brush behavior — the release point already coincides with
    // the last move).
    ASSERT_EQ(eng.stamp_count(), 3U);
    EXPECT_FLOAT_EQ(eng.stamps()[0].x_px, 10.0F);
    EXPECT_FLOAT_EQ(eng.stamps()[0].y_px, 20.0F);
    EXPECT_FLOAT_EQ(eng.stamps()[1].x_px, 11.0F);
    EXPECT_FLOAT_EQ(eng.stamps()[2].x_px, 12.0F);
}

TEST(StrokeEngine, MovesAfterReleaseAreIgnored) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::left);
    eng.inject_move_(5.0, 5.0);  // mouse moves off-canvas with no button — drop
    eng.inject_move_(6.0, 6.0);
    EXPECT_EQ(eng.stamp_count(), 2U);  // press + first move only
}

TEST(StrokeEngine, RightButtonDoesNotDraw) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::right);
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::right);
    EXPECT_EQ(eng.stamp_count(), 0U);
    EXPECT_FALSE(eng.is_drawing());
}

TEST(StrokeEngine, OverlappingButtonsLeftOwnsTheStroke) {
    // Reasonable behavior: left starts a stroke, a stray right press does
    // not end it, a left release does. Mirrors how most paint tools behave.
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_press_(0.0, 0.0, PB::right);  // shouldn't toggle drawing_
    EXPECT_TRUE(eng.is_drawing());
    eng.inject_move_(1.0, 1.0);
    eng.inject_release_(1.0, 1.0, PB::right);  // wrong button — no-op
    EXPECT_TRUE(eng.is_drawing());
    eng.inject_release_(1.0, 1.0, PB::left);
    EXPECT_FALSE(eng.is_drawing());
}

TEST(StrokeEngine, ClearStampsResetsBuffer) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left);
    eng.inject_move_(1.0, 1.0);
    EXPECT_EQ(eng.stamp_count(), 2U);
    eng.clear_stamps();
    EXPECT_EQ(eng.stamp_count(), 0U);
    // is_drawing() should be unaffected — clear() purges the buffer,
    // not the drag state.
    EXPECT_TRUE(eng.is_drawing());
}

TEST(StrokeEngine, ResizeUpdatesCanvasSize) {
    auto eng = make_test_engine();
    eng.inject_resize_(800U, 600U);
    // No public getter for canvas_w_/h_ — but if record() were callable
    // without a pipeline it'd use these. The smoke test here is just
    // that the event handler runs without UB / crash.
    SUCCEED();
}

TEST(StrokeEngine, StampPressureFullScale) {
    auto eng = make_test_engine();
    eng.inject_press_(0.0, 0.0, PB::left, /*pressure=*/1.0F);
    ASSERT_EQ(eng.stamp_count(), 1U);
    EXPECT_FLOAT_EQ(eng.stamps()[0].a, 1.0F);
    // Default brush max radius is 10px; pressure=1 → r = 10.
    EXPECT_FLOAT_EQ(eng.stamps()[0].radius_px, 10.0F);
}

// ---- stamp_from_pressure: pure mapping coverage ----

TEST(StrokeEnginePressure, RadiusLerpsLinearly) {
    BrushStyle style{};
    style.min_radius_px = 2.0F;
    style.max_radius_px = 10.0F;
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 0.0F).radius_px,  2.0F);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 1.0F).radius_px, 10.0F);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 0.5F).radius_px,  6.0F);
}

TEST(StrokeEnginePressure, AlphaUsesGammaCurve) {
    BrushStyle style{};
    style.a           = 1.0F;
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
    style.min_radius_px  = 0.5F;
    style.max_radius_px  = 50.0F;
    style.softness_ratio = 0.20F;
    // Tiny radius (0.5px) → floor of 1px kicks in.
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 0.0F).softness_px, 1.0F);
    // Big radius → 20% of 50 = 10px.
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, 1.0F).softness_px, 10.0F);
}

TEST(StrokeEnginePressure, PressureClampedToZeroOne) {
    BrushStyle style{};
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, -0.5F).radius_px,
                    style.min_radius_px);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style,  2.0F).radius_px,
                    style.max_radius_px);
    EXPECT_FLOAT_EQ(stamp_from_pressure(style, std::nanf("")).radius_px,
                    style.min_radius_px);
}

TEST(StrokeEnginePressure, ColorPassesThrough) {
    BrushStyle style{};
    style.r = 0.7F; style.g = 0.3F; style.b = 0.1F; style.a = 1.0F;
    const auto s = stamp_from_pressure(style, 0.8F);
    EXPECT_FLOAT_EQ(s.r, 0.7F);
    EXPECT_FLOAT_EQ(s.g, 0.3F);
    EXPECT_FLOAT_EQ(s.b, 0.1F);
}

TEST(StrokeEnginePressure, EngineAppliesBrushOnPress) {
    BrushStyle style{};
    style.min_radius_px = 1.0F;
    style.max_radius_px = 20.0F;
    style.alpha_gamma   = 1.0F;
    StrokeEngine eng{StrokeEngine::TestingTag{}, style};
    eng.inject_press_(50.0, 60.0, PB::left, /*pressure=*/0.5F);
    eng.inject_move_(51.0, 61.0, /*pressure=*/0.75F);
    ASSERT_EQ(eng.stamp_count(), 2U);
    EXPECT_FLOAT_EQ(eng.stamps()[0].x_px,        50.0F);
    EXPECT_FLOAT_EQ(eng.stamps()[0].radius_px,   10.5F);  // 1 + (20-1)*0.5
    EXPECT_NEAR    (eng.stamps()[0].a,           0.5F, 1e-5F);
    EXPECT_FLOAT_EQ(eng.stamps()[1].radius_px,   1.0F + 19.0F * 0.75F);
}

TEST(StrokeEnginePressure, SetBrushMutatesLiveStyle) {
    StrokeEngine eng{StrokeEngine::TestingTag{}};
    BrushStyle s{};
    s.min_radius_px = 5.0F;
    s.max_radius_px = 5.0F;  // constant 5px regardless of pressure
    eng.set_brush(s);
    eng.inject_press_(0.0, 0.0, PB::left, 0.1F);
    eng.inject_move_(1.0, 1.0, 1.0F);
    ASSERT_EQ(eng.stamp_count(), 2U);
    EXPECT_FLOAT_EQ(eng.stamps()[0].radius_px, 5.0F);
    EXPECT_FLOAT_EQ(eng.stamps()[1].radius_px, 5.0F);
}
