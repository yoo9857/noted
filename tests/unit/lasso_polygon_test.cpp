#include <gtest/gtest.h>

#include "noted/domain/selection/selection.hpp"

namespace {

using noted::domain::LassoPolygon;
using noted::domain::Point2i;
using noted::domain::Selection;
using noted::domain::SelectionRect;

[[nodiscard]] auto make_square(int x, int y, int side) -> LassoPolygon {
    return LassoPolygon{{
        Point2i{x, y},
        Point2i{x + side, y},
        Point2i{x + side, y + side},
        Point2i{x, y + side},
    }};
}

}  // namespace

// ---- Empty / degenerate ----------------------------------------------------

TEST(LassoPolygon, DefaultIsEmpty) {
    LassoPolygon p;
    EXPECT_TRUE(p.is_empty());
    EXPECT_EQ(p.size(), 0U);
    EXPECT_FALSE(p.aabb());
    EXPECT_FALSE(p.contains(0, 0));
}

TEST(LassoPolygon, TwoVerticesAreEmpty) {
    LassoPolygon p{{Point2i{0, 0}, Point2i{10, 10}}};
    EXPECT_TRUE(p.is_empty());
    EXPECT_FALSE(p.contains(5, 5));
}

// ---- AABB ------------------------------------------------------------------

TEST(LassoPolygon, AABBForSquare) {
    auto p = make_square(10, 20, 30);
    auto bb = p.aabb();
    ASSERT_TRUE(bb);
    EXPECT_EQ(bb->x, 10);
    EXPECT_EQ(bb->y, 20);
    EXPECT_EQ(bb->width, 30);
    EXPECT_EQ(bb->height, 30);
}

TEST(LassoPolygon, AABBForTriangle) {
    LassoPolygon p{{Point2i{0, 0}, Point2i{10, 0}, Point2i{5, 8}}};
    auto bb = p.aabb();
    ASSERT_TRUE(bb);
    EXPECT_EQ(bb->x, 0);
    EXPECT_EQ(bb->y, 0);
    EXPECT_EQ(bb->width, 10);
    EXPECT_EQ(bb->height, 8);
}

// ---- contains() ------------------------------------------------------------

TEST(LassoPolygonContains, SquareInteriorAndCorners) {
    auto p = make_square(0, 0, 10);
    EXPECT_TRUE(p.contains(5, 5));    // interior
    EXPECT_FALSE(p.contains(15, 5));  // outside on right
    EXPECT_FALSE(p.contains(-1, 5));  // outside on left
    EXPECT_FALSE(p.contains(5, -1));  // outside on top
    EXPECT_FALSE(p.contains(5, 15));  // outside on bottom
}

TEST(LassoPolygonContains, ConcavePolygonRespectsShape) {
    // L-shaped polygon — proves we follow the actual boundary, not
    // just the AABB.
    //  (0,0)──────(10,0)
    //    │           │
    //    │  (5,5)───(10,5)        <-- inset on right side
    //    │     │
    //  (0,10)─(5,10)
    LassoPolygon p{{
        Point2i{0, 0},
        Point2i{10, 0},
        Point2i{10, 5},
        Point2i{5, 5},
        Point2i{5, 10},
        Point2i{0, 10},
    }};
    EXPECT_TRUE(p.contains(2, 2));   // in the top-left
    EXPECT_TRUE(p.contains(2, 8));   // in the bottom-left
    EXPECT_FALSE(p.contains(8, 8));  // in the cut-out — must be outside
    EXPECT_TRUE(p.contains(8, 2));   // in the top-right (above the cut)
}

TEST(LassoPolygonContains, TriangleSelectsByActualGeometry) {
    LassoPolygon p{{Point2i{0, 0}, Point2i{10, 0}, Point2i{5, 10}}};
    EXPECT_TRUE(p.contains(5, 1));    // near the top edge
    EXPECT_TRUE(p.contains(5, 5));    // centre
    EXPECT_FALSE(p.contains(0, 8));   // below the slope on the left
    EXPECT_FALSE(p.contains(10, 8));  // below the slope on the right
}

TEST(LassoPolygonContains, VertexOrderIndependent) {
    auto cw = make_square(0, 0, 10);  // clockwise
    LassoPolygon ccw{{
        // counter-clockwise
        Point2i{0, 0},
        Point2i{0, 10},
        Point2i{10, 10},
        Point2i{10, 0},
    }};
    for (int x = 1; x < 10; ++x) {
        for (int y = 1; y < 10; ++y) {
            EXPECT_EQ(cw.contains(x, y), ccw.contains(x, y));
        }
    }
}

// ---- Selection integration -------------------------------------------------

TEST(SelectionWithPolygon, FromPolygonBuildsSingletonSelection) {
    auto sel = Selection::from_polygon(make_square(0, 0, 10));
    EXPECT_FALSE(sel.is_empty());
    EXPECT_EQ(sel.polygons().size(), 1U);
    EXPECT_TRUE(sel.contains(5, 5));
    EXPECT_FALSE(sel.contains(50, 50));
}

TEST(SelectionWithPolygon, FromEmptyPolygonYieldsEmptySelection) {
    Selection sel = Selection::from_polygon(LassoPolygon{});
    EXPECT_TRUE(sel.is_empty());
}

TEST(SelectionWithPolygon, BoundsUnionsRectAndPolygon) {
    Selection sel;
    sel.add_rect(SelectionRect{0, 0, 5, 5});
    sel.add_polygon(make_square(100, 100, 20));
    auto bb = sel.bounds();
    ASSERT_TRUE(bb);
    EXPECT_EQ(bb->x, 0);
    EXPECT_EQ(bb->y, 0);
    EXPECT_EQ(bb->right(), 120);
    EXPECT_EQ(bb->bottom(), 120);
}

TEST(SelectionWithPolygon, ContainsHonoursBothRectAndPolygon) {
    Selection sel;
    sel.add_rect(SelectionRect{0, 0, 10, 10});
    sel.add_polygon(make_square(50, 50, 10));
    EXPECT_TRUE(sel.contains(5, 5));     // in rect
    EXPECT_TRUE(sel.contains(55, 55));   // in polygon
    EXPECT_FALSE(sel.contains(30, 30));  // in neither
}

TEST(SelectionWithPolygon, ClearWipesBoth) {
    Selection sel;
    sel.add_rect(SelectionRect{0, 0, 5, 5});
    sel.add_polygon(make_square(20, 20, 10));
    sel.clear();
    EXPECT_TRUE(sel.is_empty());
    EXPECT_EQ(sel.polygons().size(), 0U);
    EXPECT_EQ(sel.rects().size(), 0U);
}

TEST(SelectionWithPolygon, EqualityIncludesPolygons) {
    Selection a;
    a.add_polygon(make_square(0, 0, 10));
    Selection b;
    b.add_polygon(make_square(0, 0, 10));
    EXPECT_EQ(a, b);
    Selection c;
    c.add_polygon(make_square(0, 0, 20));
    EXPECT_NE(a, c);
}
