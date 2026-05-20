#include "noted/domain/tool/selection_drag.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "noted/domain/selection/selection.hpp"

namespace {

using noted::domain::Selection;
using noted::domain::SelectionRect;
using noted::domain::tool::apply_drag;
using noted::domain::tool::drag_mode_from_modifiers;
using noted::domain::tool::rect_from_drag;
using noted::domain::tool::SelectionDragMode;

}  // namespace

// ---- rect_from_drag --------------------------------------------------------

TEST(SelectionDragRect, ProducesCanonicalRectForwardDrag) {
    const auto r = rect_from_drag(10.0, 20.0, 50.0, 70.0);
    EXPECT_EQ(r.x, 10);
    EXPECT_EQ(r.y, 20);
    EXPECT_EQ(r.width, 40);
    EXPECT_EQ(r.height, 50);
    EXPECT_FALSE(r.is_empty());
}

TEST(SelectionDragRect, ReversedDragNormalizes) {
    // User drags from bottom-right to top-left — same rect either way.
    const auto r = rect_from_drag(80.0, 90.0, 30.0, 40.0);
    EXPECT_EQ(r.x, 30);
    EXPECT_EQ(r.y, 40);
    EXPECT_EQ(r.width, 50);
    EXPECT_EQ(r.height, 50);
}

TEST(SelectionDragRect, SubPixelDragCollapsesToEmpty) {
    // A stationary click (or a drag below 1 px on either axis) must
    // collapse to empty — `apply_drag` no-ops on empty, so a click
    // can't wipe an existing selection in replace mode.
    EXPECT_TRUE(rect_from_drag(50.0, 50.0, 50.0, 50.0).is_empty());
    EXPECT_TRUE(rect_from_drag(50.0, 50.0, 50.5, 60.0).is_empty());
    EXPECT_TRUE(rect_from_drag(50.0, 50.0, 60.0, 50.4).is_empty());
}

TEST(SelectionDragRect, NaNCoordinatesProduceEmpty) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(rect_from_drag(nan, 10.0, 50.0, 60.0).is_empty());
    EXPECT_TRUE(rect_from_drag(10.0, nan, 50.0, 60.0).is_empty());
    EXPECT_TRUE(rect_from_drag(10.0, 10.0, nan, 60.0).is_empty());
    EXPECT_TRUE(rect_from_drag(10.0, 10.0, 50.0, nan).is_empty());
}

TEST(SelectionDragRect, FractionalCoordsRoundHalfAwayFromZero) {
    // `std::llround` discipline: 9.6 → 10, 9.4 → 9, 49.5 → 50 (round
    // half away from zero — see `safe_round_to_int` in
    // selection_drag.cpp).
    const auto r = rect_from_drag(9.6, 9.4, 50.4, 49.5);
    EXPECT_EQ(r.x, 10);
    EXPECT_EQ(r.y, 9);
    EXPECT_EQ(r.width, 40);   // 50 - 10
    EXPECT_EQ(r.height, 41);  // 50 - 9
}

// ---- drag_mode_from_modifiers ---------------------------------------------

TEST(SelectionDragMode, NoModifierIsReplace) {
    EXPECT_EQ(drag_mode_from_modifiers(/*shift=*/false, /*alt=*/false), SelectionDragMode::replace);
}

TEST(SelectionDragMode, ShiftIsAdd) {
    EXPECT_EQ(drag_mode_from_modifiers(true, false), SelectionDragMode::add);
}

TEST(SelectionDragMode, AltIsSubtract) {
    EXPECT_EQ(drag_mode_from_modifiers(false, true), SelectionDragMode::subtract);
}

TEST(SelectionDragMode, ShiftPlusAltIsIntersect) {
    EXPECT_EQ(drag_mode_from_modifiers(true, true), SelectionDragMode::intersect);
}

// ---- apply_drag ------------------------------------------------------------

TEST(SelectionApply, ReplaceWipesExistingThenAdds) {
    Selection s;
    s.add_rect({.x = 0, .y = 0, .width = 50, .height = 50});
    apply_drag(s, {.x = 100, .y = 100, .width = 30, .height = 30}, SelectionDragMode::replace);
    ASSERT_EQ(s.rects().size(), 1U);
    EXPECT_EQ(s.rects()[0].x, 100);
    EXPECT_EQ(s.rects()[0].width, 30);
}

TEST(SelectionApply, EmptyRectIsNoOpInReplace) {
    // Stationary click must NOT wipe an existing selection.
    Selection s;
    s.add_rect({.x = 0, .y = 0, .width = 50, .height = 50});
    apply_drag(s, SelectionRect{}, SelectionDragMode::replace);
    EXPECT_EQ(s.rects().size(), 1U);
}

TEST(SelectionApply, AddUnionsRectIntoExisting) {
    Selection s;
    s.add_rect({.x = 0, .y = 0, .width = 50, .height = 50});
    apply_drag(s, {.x = 100, .y = 100, .width = 30, .height = 30}, SelectionDragMode::add);
    EXPECT_EQ(s.rects().size(), 2U);
}

TEST(SelectionApply, SubtractRemovesOverlap) {
    Selection s;
    s.add_rect({.x = 0, .y = 0, .width = 100, .height = 100});
    // Subtract a vertical strip down the middle — the remaining
    // pieces are left + right bands. Selection::subtract_rect docs
    // promise "up to four surviving pieces"; for this case it's two.
    apply_drag(s, {.x = 40, .y = 0, .width = 20, .height = 100}, SelectionDragMode::subtract);
    EXPECT_GE(s.rects().size(), 2U);
    // Every remaining rect should be entirely outside the subtracted
    // strip.
    for (const auto& r : s.rects()) {
        EXPECT_TRUE(r.right() <= 40 || r.x >= 60);
    }
}

TEST(SelectionApply, IntersectKeepsOnlyOverlap) {
    Selection s;
    s.add_rect({.x = 0, .y = 0, .width = 100, .height = 100});
    apply_drag(s, {.x = 50, .y = 50, .width = 100, .height = 100}, SelectionDragMode::intersect);
    ASSERT_EQ(s.rects().size(), 1U);
    EXPECT_EQ(s.rects()[0].x, 50);
    EXPECT_EQ(s.rects()[0].y, 50);
    EXPECT_EQ(s.rects()[0].width, 50);
    EXPECT_EQ(s.rects()[0].height, 50);
}
