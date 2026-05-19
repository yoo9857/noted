#include "noted/engine/canvas/page.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

namespace {

using noted::canvas::Page;
using noted::canvas::PageBackground;
using noted::canvas::PageList;

[[nodiscard]] auto near_f(float a, float b, float eps = 1e-4F) -> bool {
    return std::abs(a - b) <= eps;
}

}  // namespace

// ---- Empty list ------------------------------------------------------------

TEST(PageList, EmptyDefaults) {
    PageList list;
    EXPECT_TRUE(list.empty());
    EXPECT_EQ(list.size(), 0U);
    EXPECT_FLOAT_EQ(list.total_height_px(), 0.0F);
    EXPECT_FLOAT_EQ(list.max_width_px(), 0.0F);
    EXPECT_FLOAT_EQ(list.gap_px(), PageList::kDefaultGapPx);
    EXPECT_TRUE(list.pages().empty());
}

TEST(PageList, ConstructorGapClamped) {
    PageList neg{-50.0F};
    EXPECT_FLOAT_EQ(neg.gap_px(), 0.0F);

    PageList nan{std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FLOAT_EQ(nan.gap_px(), 0.0F);

    PageList zero{0.0F};
    EXPECT_FLOAT_EQ(zero.gap_px(), 0.0F);  // 0 is allowed — continuous scroll

    PageList pos{40.0F};
    EXPECT_FLOAT_EQ(pos.gap_px(), 40.0F);
}

// ---- Adding pages ----------------------------------------------------------

TEST(PageList, AddPageReturnsIndex) {
    PageList list;
    EXPECT_EQ(list.add_page(612.0F, 792.0F, PageBackground::blank), 0U);
    EXPECT_EQ(list.add_page(612.0F, 792.0F, PageBackground::lined), 1U);
    EXPECT_EQ(list.size(), 2U);
}

TEST(PageList, FirstPageOriginIsZero) {
    PageList list;
    list.add_page(612.0F, 792.0F, PageBackground::blank);
    EXPECT_FLOAT_EQ(list.pages()[0].origin_x_px, 0.0F);
    EXPECT_FLOAT_EQ(list.pages()[0].origin_y_px, 0.0F);
}

TEST(PageList, SecondPageOriginIsOffsetByFirstHeightPlusGap) {
    PageList list{20.0F};
    list.add_page(612.0F, 792.0F, PageBackground::blank);
    list.add_page(612.0F, 792.0F, PageBackground::grid);
    // First: y = 0, second: y = 792 + 20 = 812.
    EXPECT_FLOAT_EQ(list.pages()[0].origin_y_px, 0.0F);
    EXPECT_FLOAT_EQ(list.pages()[1].origin_y_px, 812.0F);
}

TEST(PageList, MixedHeightsStackCorrectly) {
    PageList list{10.0F};
    list.add_page(612.0F, 100.0F, PageBackground::blank);  // y=0
    list.add_page(612.0F, 200.0F, PageBackground::grid);   // y=110
    list.add_page(612.0F, 50.0F, PageBackground::lined);   // y=320
    EXPECT_FLOAT_EQ(list.pages()[0].origin_y_px, 0.0F);
    EXPECT_FLOAT_EQ(list.pages()[1].origin_y_px, 110.0F);
    EXPECT_FLOAT_EQ(list.pages()[2].origin_y_px, 320.0F);
}

TEST(PageList, BackgroundIsStored) {
    PageList list;
    list.add_page(100.0F, 100.0F, PageBackground::blank);
    list.add_page(100.0F, 100.0F, PageBackground::lined);
    list.add_page(100.0F, 100.0F, PageBackground::grid);
    list.add_page(100.0F, 100.0F, PageBackground::dotted);
    EXPECT_EQ(list.pages()[0].background, PageBackground::blank);
    EXPECT_EQ(list.pages()[1].background, PageBackground::lined);
    EXPECT_EQ(list.pages()[2].background, PageBackground::grid);
    EXPECT_EQ(list.pages()[3].background, PageBackground::dotted);
}

// ---- Extent clamping -------------------------------------------------------

TEST(PageList, NegativeExtentsClampedToOnePixel) {
    PageList list;
    list.add_page(-50.0F, -100.0F, PageBackground::blank);
    EXPECT_FLOAT_EQ(list.pages()[0].extent_w_px, 1.0F);
    EXPECT_FLOAT_EQ(list.pages()[0].extent_h_px, 1.0F);
}

TEST(PageList, ZeroExtentClampedToOnePixel) {
    PageList list;
    list.add_page(0.0F, 0.0F, PageBackground::blank);
    EXPECT_FLOAT_EQ(list.pages()[0].extent_w_px, 1.0F);
    EXPECT_FLOAT_EQ(list.pages()[0].extent_h_px, 1.0F);
}

TEST(PageList, NanExtentClampedToOnePixel) {
    PageList list;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    list.add_page(nan, nan, PageBackground::blank);
    EXPECT_FLOAT_EQ(list.pages()[0].extent_w_px, 1.0F);
    EXPECT_FLOAT_EQ(list.pages()[0].extent_h_px, 1.0F);
}

// ---- total_height / max_width ---------------------------------------------

TEST(PageList, TotalHeightSumsExtentsPlusGaps) {
    PageList list{15.0F};
    list.add_page(100.0F, 200.0F, PageBackground::blank);
    list.add_page(100.0F, 300.0F, PageBackground::blank);
    list.add_page(100.0F, 150.0F, PageBackground::blank);
    // 200 + 300 + 150 + 2 * 15 = 680.
    EXPECT_FLOAT_EQ(list.total_height_px(), 680.0F);
}

TEST(PageList, SinglePageTotalHeightHasNoGap) {
    PageList list{20.0F};
    list.add_page(100.0F, 500.0F, PageBackground::blank);
    EXPECT_FLOAT_EQ(list.total_height_px(), 500.0F);
}

TEST(PageList, MaxWidthIsLargestExtent) {
    PageList list;
    list.add_page(400.0F, 100.0F, PageBackground::blank);
    list.add_page(800.0F, 100.0F, PageBackground::blank);
    list.add_page(600.0F, 100.0F, PageBackground::blank);
    EXPECT_FLOAT_EQ(list.max_width_px(), 800.0F);
}

// ---- Removing pages --------------------------------------------------------

TEST(PageList, RemoveMiddleReflowsLowerPages) {
    PageList list{10.0F};
    list.add_page(100.0F, 100.0F, PageBackground::blank);  // y=0
    list.add_page(100.0F, 100.0F, PageBackground::grid);   // y=110
    list.add_page(100.0F, 100.0F, PageBackground::lined);  // y=220

    list.remove_page(1);  // drop the grid page

    EXPECT_EQ(list.size(), 2U);
    EXPECT_EQ(list.pages()[0].background, PageBackground::blank);
    EXPECT_EQ(list.pages()[1].background, PageBackground::lined);
    EXPECT_FLOAT_EQ(list.pages()[0].origin_y_px, 0.0F);
    EXPECT_FLOAT_EQ(list.pages()[1].origin_y_px, 110.0F);
}

TEST(PageList, RemoveFirstReflowsAll) {
    PageList list{10.0F};
    list.add_page(100.0F, 100.0F, PageBackground::blank);
    list.add_page(100.0F, 200.0F, PageBackground::grid);
    list.add_page(100.0F, 300.0F, PageBackground::lined);

    list.remove_page(0);

    EXPECT_EQ(list.size(), 2U);
    EXPECT_FLOAT_EQ(list.pages()[0].origin_y_px, 0.0F);
    EXPECT_FLOAT_EQ(list.pages()[1].origin_y_px, 210.0F);  // 200 + 10
}

TEST(PageList, RemoveLastShortensTotal) {
    PageList list{10.0F};
    list.add_page(100.0F, 100.0F, PageBackground::blank);
    list.add_page(100.0F, 200.0F, PageBackground::grid);
    EXPECT_FLOAT_EQ(list.total_height_px(), 310.0F);

    list.remove_page(1);
    EXPECT_EQ(list.size(), 1U);
    EXPECT_FLOAT_EQ(list.total_height_px(), 100.0F);
}

TEST(PageList, RemoveOutOfRangeIsNoOp) {
    PageList list;
    list.add_page(100.0F, 100.0F, PageBackground::blank);
    list.remove_page(99);
    EXPECT_EQ(list.size(), 1U);
}

TEST(PageList, RemoveOnEmptyIsNoOp) {
    PageList list;
    list.remove_page(0);
    EXPECT_TRUE(list.empty());
}

// ---- Wire-stable enum ordinals --------------------------------------------

TEST(PageBackgroundOrdinals, MatchSchemaContract) {
    // PageBackground ordinals are wire-stable for the future
    // .noted v2 schema. Pin them here so a reorder doesn't slip
    // through review.
    EXPECT_EQ(static_cast<int>(PageBackground::blank), 0);
    EXPECT_EQ(static_cast<int>(PageBackground::lined), 1);
    EXPECT_EQ(static_cast<int>(PageBackground::grid), 2);
    EXPECT_EQ(static_cast<int>(PageBackground::dotted), 3);
}
