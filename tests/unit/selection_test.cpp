#include "noted/domain/selection/selection.hpp"

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

namespace {

using noted::domain::intersect;
using noted::domain::Selection;
using noted::domain::SelectionRect;

// Build a Selection from a list of rects, easing the test boilerplate.
auto make(std::initializer_list<SelectionRect> rs) -> Selection {
    Selection s;
    for (const auto& r : rs) {
        s.add_rect(r);
    }
    return s;
}

}  // namespace

// ---- SelectionRect predicates ----------------------------------------------

TEST(SelectionRect, EmptyDetection) {
    EXPECT_TRUE((SelectionRect{0, 0, 0, 10}.is_empty()));
    EXPECT_TRUE((SelectionRect{0, 0, 10, 0}.is_empty()));
    EXPECT_TRUE((SelectionRect{0, 0, -5, 5}.is_empty()));
    EXPECT_FALSE((SelectionRect{0, 0, 1, 1}.is_empty()));
}

TEST(SelectionRect, ContainsIsHalfOpen) {
    constexpr SelectionRect r{10, 20, 5, 5};
    EXPECT_TRUE(r.contains(10, 20));   // top-left inclusive
    EXPECT_TRUE(r.contains(14, 24));   // inside
    EXPECT_FALSE(r.contains(15, 24));  // right edge exclusive
    EXPECT_FALSE(r.contains(14, 25));  // bottom edge exclusive
    EXPECT_FALSE(r.contains(9, 20));   // outside
}

// ---- intersect(SelectionRect, SelectionRect) -------------------------------

TEST(IntersectRect, EmptyInputYieldsNullopt) {
    EXPECT_FALSE(intersect({0, 0, 0, 10}, {0, 0, 10, 10}).has_value());
    EXPECT_FALSE(intersect({0, 0, 10, 10}, {0, 0, 10, 0}).has_value());
}

TEST(IntersectRect, DisjointYieldsNullopt) {
    EXPECT_FALSE(intersect({0, 0, 5, 5}, {10, 10, 5, 5}).has_value());
}

TEST(IntersectRect, EdgeTouchIsEmpty) {
    // Right edge of A coincides with left edge of B — half-open
    // semantics mean intersect is empty.
    EXPECT_FALSE(intersect({0, 0, 5, 5}, {5, 0, 5, 5}).has_value());
}

TEST(IntersectRect, PartialOverlap) {
    auto r = intersect({0, 0, 10, 10}, {5, 5, 10, 10});
    ASSERT_TRUE(r);
    EXPECT_EQ(*r, (SelectionRect{5, 5, 5, 5}));
}

TEST(IntersectRect, ContainmentYieldsInner) {
    auto r = intersect({0, 0, 100, 100}, {10, 10, 5, 5});
    ASSERT_TRUE(r);
    EXPECT_EQ(*r, (SelectionRect{10, 10, 5, 5}));
}

// ---- Selection::from_rect --------------------------------------------------

TEST(Selection, FromEmptyRectIsEmpty) {
    EXPECT_TRUE(Selection::from_rect({0, 0, 0, 5}).is_empty());
    EXPECT_TRUE(Selection{}.is_empty());
}

TEST(Selection, FromRectStoresOne) {
    auto s = Selection::from_rect({1, 2, 3, 4});
    EXPECT_EQ(s.size(), 1U);
    EXPECT_FALSE(s.is_empty());
}

// ---- add_rect: empty filtered, normalized order ----------------------------

TEST(Selection, AddRectFiltersEmpty) {
    Selection s;
    s.add_rect({0, 0, 0, 10});
    s.add_rect({0, 0, -1, 1});
    EXPECT_TRUE(s.is_empty());
}

TEST(Selection, AddRectKeepsCanonicalOrder) {
    // Insert in scrambled order; canonical sort is by (y, x, height, width)
    // so two equivalent normalized selections compare equal.
    auto s = make({
        {10, 5, 1, 1},  // y=5
        {0, 0, 1, 1},   // y=0, x=0, h=1, w=1
        {0, 10, 1, 1},  // y=10
        {0, 0, 2, 1},   // y=0, x=0, h=1, w=2
    });
    ASSERT_EQ(s.size(), 4U);
    EXPECT_EQ(s.rects()[0], (SelectionRect{0, 0, 1, 1}));
    EXPECT_EQ(s.rects()[1], (SelectionRect{0, 0, 2, 1}));
    EXPECT_EQ(s.rects()[2], (SelectionRect{10, 5, 1, 1}));
    EXPECT_EQ(s.rects()[3], (SelectionRect{0, 10, 1, 1}));
}

TEST(Selection, AddRectDedupsExactDuplicates) {
    Selection s;
    s.add_rect({0, 0, 10, 10});
    s.add_rect({0, 0, 10, 10});
    s.add_rect({0, 0, 10, 10});
    EXPECT_EQ(s.size(), 1U);
}

// ---- bounds -----------------------------------------------------------------

TEST(Selection, BoundsEmpty) {
    EXPECT_FALSE(Selection{}.bounds().has_value());
}

TEST(Selection, BoundsSingleRect) {
    auto b = Selection::from_rect({3, 4, 5, 6}).bounds();
    ASSERT_TRUE(b);
    EXPECT_EQ(*b, (SelectionRect{3, 4, 5, 6}));
}

TEST(Selection, BoundsMultiRectIsTightEnclosure) {
    auto s = make({{0, 0, 5, 5}, {10, 10, 5, 5}, {20, 0, 1, 30}});
    auto b = s.bounds();
    ASSERT_TRUE(b);
    EXPECT_EQ(b->x, 0);
    EXPECT_EQ(b->y, 0);
    EXPECT_EQ(b->right(), 21);
    EXPECT_EQ(b->bottom(), 30);
}

// ---- contains ---------------------------------------------------------------

TEST(Selection, ContainsAcrossDisjointRects) {
    auto s = make({{0, 0, 5, 5}, {20, 20, 5, 5}});
    EXPECT_TRUE(s.contains(2, 2));
    EXPECT_TRUE(s.contains(22, 22));
    EXPECT_FALSE(s.contains(10, 10));
    EXPECT_FALSE(s.contains(25, 25));  // exclusive right/bottom
}

// ---- intersect_rect --------------------------------------------------------

TEST(Selection, IntersectRectShrinksConstituents) {
    auto s = make({{0, 0, 10, 10}, {20, 20, 10, 10}});
    s.intersect_rect({5, 5, 20, 20});
    // Expect:
    //   (0,0,10,10) ∩ (5,5,20,20) = (5,5,5,5)
    //   (20,20,10,10) ∩ (5,5,20,20) = (20,20,5,5)
    ASSERT_EQ(s.size(), 2U);
    EXPECT_EQ(s.rects()[0], (SelectionRect{5, 5, 5, 5}));
    EXPECT_EQ(s.rects()[1], (SelectionRect{20, 20, 5, 5}));
}

TEST(Selection, IntersectRectWithEmptyClears) {
    auto s = make({{0, 0, 10, 10}});
    s.intersect_rect({0, 0, 0, 0});
    EXPECT_TRUE(s.is_empty());
}

TEST(Selection, IntersectRectWithDisjointEmpties) {
    auto s = make({{0, 0, 5, 5}});
    s.intersect_rect({100, 100, 1, 1});
    EXPECT_TRUE(s.is_empty());
}

// ---- subtract_rect ---------------------------------------------------------

TEST(Selection, SubtractFullCoverClears) {
    auto s = make({{10, 10, 5, 5}});
    s.subtract_rect({0, 0, 100, 100});
    EXPECT_TRUE(s.is_empty());
}

TEST(Selection, SubtractNoOverlapIsIdentity) {
    auto s = make({{0, 0, 5, 5}});
    const auto before = s;
    s.subtract_rect({100, 100, 1, 1});
    EXPECT_EQ(s, before);
}

TEST(Selection, SubtractCenterCreatesFourPieces) {
    auto s = make({{0, 0, 10, 10}});
    s.subtract_rect({4, 4, 2, 2});
    // Expected pieces (sorted by y, x, h, w):
    //   top   : (0,0,10,4)
    //   left  : (0,4,4,2)
    //   right : (6,4,4,2)
    //   bottom: (0,6,10,4)
    ASSERT_EQ(s.size(), 4U);
    std::vector<SelectionRect> expected{
        {0, 0, 10, 4},
        {0, 4, 4, 2},
        {6, 4, 4, 2},
        {0, 6, 10, 4},
    };
    // Compare as multisets in normalized order.
    std::sort(expected.begin(), expected.end(), [](auto a, auto b) {
        if (a.y != b.y)
            return a.y < b.y;
        if (a.x != b.x)
            return a.x < b.x;
        if (a.height != b.height)
            return a.height < b.height;
        return a.width < b.width;
    });
    EXPECT_EQ(s.rects(), expected);
}

TEST(Selection, SubtractCornerCreatesLPiece) {
    auto s = make({{0, 0, 10, 10}});
    // Remove the bottom-right 5×5.
    s.subtract_rect({5, 5, 5, 5});
    // Expected: top band (0,0,10,5) and left strip (0,5,5,5).
    ASSERT_EQ(s.size(), 2U);
    EXPECT_EQ(s.rects()[0], (SelectionRect{0, 0, 10, 5}));
    EXPECT_EQ(s.rects()[1], (SelectionRect{0, 5, 5, 5}));
}

// ---- chained ops + equality ------------------------------------------------

TEST(Selection, AddIntersectSubtractRoundTrip) {
    Selection a = make({{0, 0, 100, 100}});
    a.intersect_rect({20, 20, 60, 60});  // becomes (20,20,60,60)
    a.subtract_rect({40, 40, 20, 20});   // hole in middle
    // bounds should still be the intersect rect.
    auto b = a.bounds();
    ASSERT_TRUE(b);
    EXPECT_EQ(*b, (SelectionRect{20, 20, 60, 60}));
    // The center hole should not be contained.
    EXPECT_FALSE(a.contains(50, 50));
    EXPECT_TRUE(a.contains(25, 25));
}

TEST(Selection, EqualityIsByCanonicalContents) {
    auto a = make({{0, 0, 5, 5}, {10, 10, 5, 5}});
    auto b = make({{10, 10, 5, 5}, {0, 0, 5, 5}});  // insertion order swapped
    EXPECT_EQ(a, b);
}
