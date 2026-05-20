#include "noted/domain/tool/shape_drag.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace {

using noted::domain::tool::bounds_from_drag;
using noted::domain::tool::shape_from_drag;
using noted::domain::tool::ShapeKind;
using noted::domain::tool::ShapeOptions;

}  // namespace

// ---- bounds_from_drag ------------------------------------------------------

TEST(ShapeBounds, CanonicalForwardDrag) {
    auto r = bounds_from_drag(10.0, 20.0, 50.0, 70.0);
    ASSERT_TRUE(r.has_value());
    const auto [x0, y0, x1, y1] = *r;
    EXPECT_DOUBLE_EQ(x0, 10.0);
    EXPECT_DOUBLE_EQ(y0, 20.0);
    EXPECT_DOUBLE_EQ(x1, 50.0);
    EXPECT_DOUBLE_EQ(y1, 70.0);
}

TEST(ShapeBounds, ReversedDragNormalizes) {
    auto r = bounds_from_drag(80.0, 90.0, 30.0, 40.0);
    ASSERT_TRUE(r.has_value());
    const auto [x0, y0, x1, y1] = *r;
    EXPECT_DOUBLE_EQ(x0, 30.0);
    EXPECT_DOUBLE_EQ(y0, 40.0);
    EXPECT_DOUBLE_EQ(x1, 80.0);
    EXPECT_DOUBLE_EQ(y1, 90.0);
}

TEST(ShapeBounds, SubPixelDragIsNullopt) {
    EXPECT_FALSE(bounds_from_drag(50.0, 50.0, 50.0, 50.0).has_value());
    EXPECT_FALSE(bounds_from_drag(50.0, 50.0, 50.5, 60.0).has_value());
    EXPECT_FALSE(bounds_from_drag(50.0, 50.0, 60.0, 50.4).has_value());
}

TEST(ShapeBounds, NaNOrInfIsNullopt) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(bounds_from_drag(nan, 10.0, 50.0, 60.0).has_value());
    EXPECT_FALSE(bounds_from_drag(10.0, 10.0, inf, 60.0).has_value());
}

// ---- shape_from_drag -------------------------------------------------------

TEST(ShapeFromDrag, PicksKindFromOptions) {
    ShapeOptions opt{};
    opt.kind = ShapeKind::ellipse;
    auto s = shape_from_drag(10.0, 20.0, 100.0, 80.0, opt);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->kind, ShapeKind::ellipse);
}

TEST(ShapeFromDrag, SnapshotsColourAndWidth) {
    ShapeOptions opt{};
    opt.stroke_r = 1.0F;
    opt.stroke_g = 0.5F;
    opt.stroke_b = 0.0F;
    opt.stroke_a = 0.75F;
    opt.stroke_width_px = 5.0F;
    auto s = shape_from_drag(0.0, 0.0, 100.0, 100.0, opt);
    ASSERT_TRUE(s.has_value());
    EXPECT_FLOAT_EQ(s->stroke_r, 1.0F);
    EXPECT_FLOAT_EQ(s->stroke_g, 0.5F);
    EXPECT_FLOAT_EQ(s->stroke_b, 0.0F);
    EXPECT_FLOAT_EQ(s->stroke_a, 0.75F);
    EXPECT_FLOAT_EQ(s->stroke_width_px, 5.0F);
}

TEST(ShapeFromDrag, ClampsStrokeWidthFloor) {
    ShapeOptions opt{};
    opt.stroke_width_px = -1.0F;
    auto s = shape_from_drag(0.0, 0.0, 50.0, 50.0, opt);
    ASSERT_TRUE(s.has_value());
    EXPECT_FLOAT_EQ(s->stroke_width_px, 0.5F);

    opt.stroke_width_px = std::numeric_limits<float>::quiet_NaN();
    auto s2 = shape_from_drag(0.0, 0.0, 50.0, 50.0, opt);
    ASSERT_TRUE(s2.has_value());
    EXPECT_FLOAT_EQ(s2->stroke_width_px, 0.5F);
}

TEST(ShapeFromDrag, SubPixelIsNullopt) {
    ShapeOptions opt{};
    EXPECT_FALSE(shape_from_drag(50.0, 50.0, 50.0, 50.0, opt).has_value());
}

TEST(ShapePrimitive, EmptyDetectsZeroAreaRect) {
    using noted::domain::tool::ShapePrimitive;
    ShapePrimitive p{};
    p.x0 = 10.0;
    p.x1 = 10.5;  // sub-pixel width
    p.y0 = 0.0;
    p.y1 = 50.0;
    EXPECT_TRUE(p.is_empty());

    p.x1 = 11.0;
    EXPECT_FALSE(p.is_empty());
}

TEST(ShapeKindOrdinals, WireStable) {
    // .noted will persist these once shapes move into the document.
    EXPECT_EQ(static_cast<int>(ShapeKind::rectangle), 0);
    EXPECT_EQ(static_cast<int>(ShapeKind::ellipse), 1);
}
