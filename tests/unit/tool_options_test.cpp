#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "noted/domain/tool/options.hpp"
#include "noted/domain/tool/tool.hpp"

namespace {

using noted::domain::tool::brush_from_eraser;
using noted::domain::tool::brush_from_pen;
using noted::domain::tool::EraserOptions;
using noted::domain::tool::PenOptions;
using noted::domain::tool::ToolState;

}  // namespace

// ---- PenOptions defaults match BrushStyle defaults bit-for-bit -----------

TEST(PenOptions, DefaultsMatchEngineBrushDefaults) {
    const auto b = brush_from_pen(PenOptions{});
    EXPECT_FLOAT_EQ(b.min_radius_px, 2.0F);
    EXPECT_FLOAT_EQ(b.max_radius_px, 10.0F);
    EXPECT_FLOAT_EQ(b.alpha_gamma, 1.8F);
    EXPECT_FLOAT_EQ(b.r, 0.0F);
    EXPECT_FLOAT_EQ(b.g, 0.0F);
    EXPECT_FLOAT_EQ(b.b, 0.0F);
    EXPECT_FLOAT_EQ(b.a, 1.0F);
}

TEST(PenOptions, ColorPassThrough) {
    PenOptions opt{};
    opt.r = 0.7F;
    opt.g = 0.3F;
    opt.b = 0.1F;
    opt.a = 0.8F;
    const auto b = brush_from_pen(opt);
    EXPECT_FLOAT_EQ(b.r, 0.7F);
    EXPECT_FLOAT_EQ(b.g, 0.3F);
    EXPECT_FLOAT_EQ(b.b, 0.1F);
    EXPECT_FLOAT_EQ(b.a, 0.8F);
}

TEST(PenOptions, NegativeRadiiClampToOnePixel) {
    PenOptions opt{};
    opt.min_radius_px = -5.0F;
    opt.max_radius_px = -1.0F;
    const auto b = brush_from_pen(opt);
    EXPECT_FLOAT_EQ(b.min_radius_px, 1.0F);
    EXPECT_FLOAT_EQ(b.max_radius_px, 1.0F);
}

TEST(PenOptions, NaNRadiiClampToOnePixel) {
    PenOptions opt{};
    opt.min_radius_px = std::numeric_limits<float>::quiet_NaN();
    opt.max_radius_px = std::numeric_limits<float>::quiet_NaN();
    const auto b = brush_from_pen(opt);
    EXPECT_FLOAT_EQ(b.min_radius_px, 1.0F);
    EXPECT_FLOAT_EQ(b.max_radius_px, 1.0F);
}

TEST(PenOptions, MinAboveMaxIsReorderedNotIgnored) {
    // User drags min slider past max — the conversion sorts them so
    // the pressure interpolation stays "min on light touch, max on
    // full press" the way every paint app does it.
    PenOptions opt{};
    opt.min_radius_px = 30.0F;
    opt.max_radius_px = 5.0F;
    const auto b = brush_from_pen(opt);
    EXPECT_FLOAT_EQ(b.min_radius_px, 5.0F);
    EXPECT_FLOAT_EQ(b.max_radius_px, 30.0F);
}

TEST(PenOptions, NonPositiveGammaCollapsesToLinear) {
    PenOptions opt{};
    opt.alpha_gamma = 0.0F;
    EXPECT_FLOAT_EQ(brush_from_pen(opt).alpha_gamma, 1.0F);
    opt.alpha_gamma = -0.5F;
    EXPECT_FLOAT_EQ(brush_from_pen(opt).alpha_gamma, 1.0F);
    opt.alpha_gamma = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(brush_from_pen(opt).alpha_gamma, 1.0F);
}

// ---- EraserOptions ---------------------------------------------------------

TEST(EraserOptions, DefaultsAreSensibleSize) {
    const auto b = brush_from_eraser(EraserOptions{});
    EXPECT_FLOAT_EQ(b.min_radius_px, 4.0F);
    EXPECT_FLOAT_EQ(b.max_radius_px, 20.0F);
    EXPECT_FLOAT_EQ(b.alpha_gamma, 1.0F);
}

TEST(EraserOptions, ColorIsAlwaysBlackOpaque) {
    // The eraser pipeline ignores src colour (destination-out only
    // reads src.alpha), but the BrushStyle still carries RGB. Pin
    // it to black so a future debug dump reads sensibly.
    EraserOptions opt{};
    const auto b = brush_from_eraser(opt);
    EXPECT_FLOAT_EQ(b.r, 0.0F);
    EXPECT_FLOAT_EQ(b.g, 0.0F);
    EXPECT_FLOAT_EQ(b.b, 0.0F);
    EXPECT_FLOAT_EQ(b.a, 1.0F);
}

TEST(EraserOptions, BadInputsClampLikePen) {
    EraserOptions opt{};
    opt.min_radius_px = -10.0F;
    opt.max_radius_px = 0.0F;
    opt.alpha_gamma = -2.0F;
    const auto b = brush_from_eraser(opt);
    EXPECT_FLOAT_EQ(b.min_radius_px, 1.0F);
    EXPECT_FLOAT_EQ(b.max_radius_px, 1.0F);
    EXPECT_FLOAT_EQ(b.alpha_gamma, 1.0F);
}

// ---- ToolState side-by-side payloads --------------------------------------

TEST(ToolState, DefaultPayloadsMatchDefaults) {
    ToolState s;
    EXPECT_EQ(s.pen, PenOptions{});
    EXPECT_EQ(s.eraser, EraserOptions{});
}

TEST(ToolState, PayloadsSurviveActiveSwitch) {
    // User picks Pen, drags colour to red, switches to Eraser, then
    // back to Pen — the Pen colour must still be red.
    ToolState s;
    s.active = noted::domain::tool::ToolKind::pen;
    s.pen.r = 1.0F;
    s.active = noted::domain::tool::ToolKind::eraser;
    s.eraser.min_radius_px = 8.0F;
    s.active = noted::domain::tool::ToolKind::pen;
    EXPECT_FLOAT_EQ(s.pen.r, 1.0F);
    EXPECT_FLOAT_EQ(s.eraser.min_radius_px, 8.0F);
}

TEST(ToolState, EqualityFollowsAllFields) {
    ToolState a;
    ToolState b;
    EXPECT_EQ(a, b);
    b.pen.r = 0.5F;
    EXPECT_NE(a, b);
    a.pen.r = 0.5F;
    EXPECT_EQ(a, b);
    b.eraser.min_radius_px = 7.0F;
    EXPECT_NE(a, b);
}
