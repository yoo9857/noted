#include "noted/compositor/selection_rasterizer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

using noted::compositor::clear_and_rasterize;
using noted::compositor::rasterize_to_buffer;
using noted::domain::Selection;
using noted::domain::SelectionRect;

// Build an N-byte mask buffer initialised to `init`.
auto make_buffer(std::size_t n, std::uint8_t init = 0) -> std::vector<std::uint8_t> {
    return std::vector<std::uint8_t>(n, init);
}

// Count pixels equal to `value`.
auto count(const std::vector<std::uint8_t>& buf, std::uint8_t value) -> std::size_t {
    return static_cast<std::size_t>(std::count(buf.begin(), buf.end(), value));
}

// Read pixel at (x, y) with row-major packed layout.
auto at(const std::vector<std::uint8_t>& buf,
        std::uint32_t w,
        std::uint32_t x,
        std::uint32_t y) -> std::uint8_t {
    return buf[(static_cast<std::size_t>(y) * w) + x];
}

}  // namespace

// ---- contract: size mismatch is silently dropped --------------------------

TEST(RasterizeToBuffer, MismatchedDestSizeIsNoOp) {
    auto sel = Selection::from_rect({0, 0, 4, 4});
    auto buf = make_buffer(15, 0);  // 4x4 needs 16 bytes
    rasterize_to_buffer(sel, {4, 4}, buf);
    EXPECT_EQ(count(buf, 255), 0U);
}

TEST(RasterizeToBuffer, ZeroExtentIsNoOp) {
    auto sel = Selection::from_rect({0, 0, 10, 10});
    auto buf = make_buffer(0);
    rasterize_to_buffer(sel, {0, 0}, buf);
    EXPECT_TRUE(buf.empty());
}

// ---- single rect: bounds + half-open contains  ----------------------------

TEST(RasterizeToBuffer, SingleRectFillsExactPixels) {
    auto sel = Selection::from_rect({2, 1, 3, 2});  // x∈[2,5), y∈[1,3)
    auto buf = make_buffer(8 * 4, 0);
    rasterize_to_buffer(sel, {8, 4}, buf);
    // Selected rect = 3 cols × 2 rows = 6 pixels.
    EXPECT_EQ(count(buf, 255), 6U);
    // Spot-check the corners — half-open: right and bottom exclusive.
    EXPECT_EQ(at(buf, 8, 2, 1), 255);
    EXPECT_EQ(at(buf, 8, 4, 2), 255);
    EXPECT_EQ(at(buf, 8, 5, 1), 0);  // right edge exclusive
    EXPECT_EQ(at(buf, 8, 2, 3), 0);  // bottom edge exclusive
}

TEST(RasterizeToBuffer, EmptySelectionLeavesBufferUntouched) {
    Selection empty;
    auto buf = make_buffer(4 * 4, 17);  // pre-fill non-zero so untouched is visible
    rasterize_to_buffer(empty, {4, 4}, buf);
    EXPECT_EQ(count(buf, 17), 16U);
}

TEST(RasterizeToBuffer, RectClippedToMaskBounds) {
    // Rect extends past every edge.
    auto sel = Selection::from_rect({-3, -2, 100, 100});
    auto buf = make_buffer(5 * 4, 0);
    rasterize_to_buffer(sel, {5, 4}, buf);
    EXPECT_EQ(count(buf, 255), 5U * 4U);  // entire mask fills
}

TEST(RasterizeToBuffer, RectFullyOutsideIsNoOp) {
    auto sel = Selection::from_rect({100, 100, 5, 5});
    auto buf = make_buffer(8 * 8, 0);
    rasterize_to_buffer(sel, {8, 8}, buf);
    EXPECT_EQ(count(buf, 255), 0U);
}

// ---- multi-rect: union via repeated fill ----------------------------------

TEST(RasterizeToBuffer, OverlappingRectsActAsUnion) {
    Selection sel;
    sel.add_rect({0, 0, 3, 3}).add_rect({2, 2, 3, 3});  // L-shape, overlap at (2,2)
    auto buf = make_buffer(5 * 5, 0);
    rasterize_to_buffer(sel, {5, 5}, buf);
    // Union = 9 + 9 - 1 overlap = 17 pixels at value 255.
    EXPECT_EQ(count(buf, 255), 17U);
}

TEST(RasterizeToBuffer, FillValueIsRespected) {
    auto sel = Selection::from_rect({0, 0, 2, 2});
    auto buf = make_buffer(2 * 2, 0);
    rasterize_to_buffer(sel, {2, 2}, buf, /*fill_value=*/64);
    EXPECT_EQ(count(buf, 64), 4U);
    EXPECT_EQ(count(buf, 255), 0U);
}

// ---- clear_and_rasterize: zeroes destination first ------------------------

TEST(ClearAndRasterize, ZeroesUnselectedPixels) {
    auto sel = Selection::from_rect({0, 0, 2, 2});
    auto buf = make_buffer(4 * 4, 200);  // pre-filled
    clear_and_rasterize(sel, {4, 4}, buf);
    EXPECT_EQ(count(buf, 255), 4U);
    EXPECT_EQ(count(buf, 0), 12U);
    EXPECT_EQ(count(buf, 200), 0U);
}

TEST(ClearAndRasterize, EmptySelectionZeroesEverything) {
    Selection empty;
    auto buf = make_buffer(3 * 3, 99);
    clear_and_rasterize(empty, {3, 3}, buf);
    EXPECT_EQ(count(buf, 0), 9U);
}

// ---- raster shape vs. Selection::contains() agreement ----------------------

TEST(RasterizeToBuffer, RasterMatchesSelectionContains) {
    // Random-ish, non-trivial layout.
    Selection sel;
    sel.add_rect({1, 1, 3, 2}).add_rect({5, 3, 2, 4}).subtract_rect({2, 2, 1, 1});

    constexpr std::uint32_t kW = 10;
    constexpr std::uint32_t kH = 8;
    auto buf = make_buffer(kW * kH, 0);
    rasterize_to_buffer(sel, {kW, kH}, buf);

    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const bool raster = at(buf, kW, x, y) == 255;
            const bool domain =
                sel.contains(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            EXPECT_EQ(raster, domain) << "mismatch at (" << x << ", " << y << ")";
        }
    }
}

// ---- polygon scanline fill (lasso step 3) ---------------------------------

namespace {
using noted::domain::LassoPolygon;
using noted::domain::Point2i;
}  // namespace

TEST(RasterizeToBuffer, PolygonSquareFillsExpectedPixels) {
    LassoPolygon poly{{
        Point2i{2, 2},
        Point2i{6, 2},
        Point2i{6, 6},
        Point2i{2, 6},
    }};
    Selection sel = Selection::from_polygon(poly);
    constexpr std::uint32_t kW = 10;
    constexpr std::uint32_t kH = 10;
    auto buf = make_buffer(kW * kH, 0);
    rasterize_to_buffer(sel, {kW, kH}, buf);
    // Interior pixels (3,3)..(5,5) should be filled. Edges may be
    // edge-rounded; assert the interior + a few outside.
    EXPECT_EQ(at(buf, kW, 4, 4), 255);
    EXPECT_EQ(at(buf, kW, 3, 3), 255);
    EXPECT_EQ(at(buf, kW, 5, 5), 255);
    EXPECT_EQ(at(buf, kW, 0, 0), 0);
    EXPECT_EQ(at(buf, kW, 8, 8), 0);
    EXPECT_EQ(at(buf, kW, 7, 4), 0);
}

TEST(RasterizeToBuffer, PolygonTriangleRespectsSlope) {
    // Triangle: (0,0), (10,0), (5,10). Filled area = lower triangle.
    LassoPolygon poly{{Point2i{0, 0}, Point2i{10, 0}, Point2i{5, 10}}};
    Selection sel = Selection::from_polygon(poly);
    constexpr std::uint32_t kW = 12;
    constexpr std::uint32_t kH = 12;
    auto buf = make_buffer(kW * kH, 0);
    rasterize_to_buffer(sel, {kW, kH}, buf);
    EXPECT_EQ(at(buf, kW, 5, 5), 255);  // middle column, halfway down — inside
    EXPECT_EQ(at(buf, kW, 0, 8), 0);    // far left, below the slope — outside
    EXPECT_EQ(at(buf, kW, 10, 8), 0);   // far right, below the slope — outside
}

TEST(RasterizeToBuffer, PolygonAndRectUnion) {
    Selection sel;
    sel.add_rect({0, 0, 4, 4});
    sel.add_polygon(LassoPolygon{{
        Point2i{6, 6},
        Point2i{10, 6},
        Point2i{10, 10},
        Point2i{6, 10},
    }});
    constexpr std::uint32_t kW = 12;
    constexpr std::uint32_t kH = 12;
    auto buf = make_buffer(kW * kH, 0);
    rasterize_to_buffer(sel, {kW, kH}, buf);
    EXPECT_EQ(at(buf, kW, 1, 1), 255);  // in rect
    EXPECT_EQ(at(buf, kW, 8, 8), 255);  // in polygon
    EXPECT_EQ(at(buf, kW, 5, 5), 0);    // in neither
}

TEST(RasterizeToBuffer, PolygonClippedToMaskBounds) {
    // Polygon extending past the mask edges — fill is clipped, not
    // skipped. (Mirrors the rect-clipping contract above.)
    LassoPolygon poly{{
        Point2i{-5, -5},
        Point2i{100, -5},
        Point2i{100, 100},
        Point2i{-5, 100},
    }};
    Selection sel = Selection::from_polygon(poly);
    constexpr std::uint32_t kW = 6;
    constexpr std::uint32_t kH = 6;
    auto buf = make_buffer(kW * kH, 0);
    rasterize_to_buffer(sel, {kW, kH}, buf);
    // Entire mask should be filled.
    EXPECT_EQ(count(buf, 255), kW * kH);
}

TEST(RasterizeToBuffer, EmptyPolygonIsNoOp) {
    LassoPolygon poly{{Point2i{0, 0}, Point2i{10, 10}}};  // < 3 vertices
    EXPECT_TRUE(poly.is_empty());
    Selection sel;
    sel.add_polygon(poly);  // ignored by Selection (is_empty short-circuits)
    auto buf = make_buffer(8 * 8, 0);
    rasterize_to_buffer(sel, {8, 8}, buf);
    EXPECT_EQ(count(buf, 255), 0U);
}
