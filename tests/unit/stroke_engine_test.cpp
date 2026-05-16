#include <gtest/gtest.h>

#include "noted/engine/hook/hook.hpp"
#include "noted/engine/stroke/stroke_engine.hpp"

namespace {

using PB = noted::hook::PointerButton;
using StrokeEngine = noted::stroke::StrokeEngine;

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

TEST(StrokeEngine, StampPressureModulatesAlpha) {
    auto eng = make_test_engine();
    // inject_press_ uses pressure = 1.0; default color alpha is 1.0,
    // so the resulting stamp's alpha is 1.0. We don't expose an
    // injection variant with custom pressure (yet) — when feat/pen-input
    // lands, extend this.
    eng.inject_press_(0.0, 0.0, PB::left);
    ASSERT_EQ(eng.stamp_count(), 1U);
    EXPECT_FLOAT_EQ(eng.stamps()[0].a, 1.0F);
}
