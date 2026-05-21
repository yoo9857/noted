#include "noted/domain/tool/image_input.hpp"

#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "noted/domain/document/asset_id.hpp"

namespace {

using noted::domain::invalid_asset_id;
using noted::domain::tool::image_primitive_from;
using noted::domain::tool::ImageOptions;
using noted::domain::tool::ImagePrimitive;

}  // namespace

// ---- image_primitive_from: identity / snapshot ----------------------------

TEST(ImagePrimitiveFrom, CopiesPositionAndOptions) {
    ImageOptions opt{};
    opt.width_px = 300.0F;
    opt.height_px = 150.0F;
    opt.r = 0.8F;
    opt.g = 0.4F;
    opt.b = 0.2F;
    opt.a = 0.9F;

    const auto p = image_primitive_from(50.0, 70.0, opt);

    EXPECT_DOUBLE_EQ(p.x, 50.0);
    EXPECT_DOUBLE_EQ(p.y, 70.0);
    EXPECT_FLOAT_EQ(p.width_px, 300.0F);
    EXPECT_FLOAT_EQ(p.height_px, 150.0F);
    EXPECT_FLOAT_EQ(p.r, 0.8F);
    EXPECT_FLOAT_EQ(p.g, 0.4F);
    EXPECT_FLOAT_EQ(p.b, 0.2F);
    EXPECT_FLOAT_EQ(p.a, 0.9F);
}

TEST(ImagePrimitiveFrom, DefaultsProduce200x200OpaqueWhite) {
    const auto p = image_primitive_from(0.0, 0.0, ImageOptions{});
    EXPECT_FLOAT_EQ(p.width_px, 200.0F);
    EXPECT_FLOAT_EQ(p.height_px, 200.0F);
    EXPECT_FLOAT_EQ(p.r, 1.0F);
    EXPECT_FLOAT_EQ(p.g, 1.0F);
    EXPECT_FLOAT_EQ(p.b, 1.0F);
    EXPECT_FLOAT_EQ(p.a, 1.0F);
}

// ---- image_primitive_from: dimension clamp --------------------------------

TEST(ImagePrimitiveFromDimensions, ClampsZeroWidthTo1px) {
    ImageOptions opt{};
    opt.width_px = 0.0F;
    const auto p = image_primitive_from(0.0, 0.0, opt);
    EXPECT_FLOAT_EQ(p.width_px, 1.0F);
}

TEST(ImagePrimitiveFromDimensions, ClampsZeroHeightTo1px) {
    ImageOptions opt{};
    opt.height_px = 0.0F;
    const auto p = image_primitive_from(0.0, 0.0, opt);
    EXPECT_FLOAT_EQ(p.height_px, 1.0F);
}

TEST(ImagePrimitiveFromDimensions, ClampsNegativeTo1px) {
    ImageOptions opt{};
    opt.width_px = -50.0F;
    opt.height_px = -1000.0F;
    const auto p = image_primitive_from(0.0, 0.0, opt);
    EXPECT_FLOAT_EQ(p.width_px, 1.0F);
    EXPECT_FLOAT_EQ(p.height_px, 1.0F);
}

TEST(ImagePrimitiveFromDimensions, ClampsNanTo1px) {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    ImageOptions opt{};
    opt.width_px = nan;
    opt.height_px = nan;
    const auto p = image_primitive_from(0.0, 0.0, opt);
    EXPECT_FLOAT_EQ(p.width_px, 1.0F);
    EXPECT_FLOAT_EQ(p.height_px, 1.0F);
}

TEST(ImagePrimitiveFromDimensions, PassesThroughValidValues) {
    ImageOptions opt{};
    opt.width_px = 1.0F;
    opt.height_px = 1.0F;
    auto p = image_primitive_from(0.0, 0.0, opt);
    EXPECT_FLOAT_EQ(p.width_px, 1.0F);
    EXPECT_FLOAT_EQ(p.height_px, 1.0F);

    opt.width_px = 4096.0F;
    opt.height_px = 4096.0F;
    p = image_primitive_from(0.0, 0.0, opt);
    EXPECT_FLOAT_EQ(p.width_px, 4096.0F);
    EXPECT_FLOAT_EQ(p.height_px, 4096.0F);
}

// ---- is_degenerate --------------------------------------------------------

TEST(ImagePrimitiveDegenerate, MinSizeIsNotDegenerate) {
    ImageOptions opt{};
    opt.width_px = 1.0F;
    opt.height_px = 1.0F;
    EXPECT_FALSE(image_primitive_from(0.0, 0.0, opt).is_degenerate());
}

TEST(ImagePrimitiveDegenerate, SubPixelWidthIsDegenerateWhenAuthoredDirectly) {
    // image_primitive_from clamps to 1, but a direct-construction
    // primitive (e.g. loaded from disk in a future PR) might be
    // smaller. is_degenerate guards the overlay against drawing
    // a zero-area rect.
    ImagePrimitive p{};
    p.width_px = 0.5F;
    EXPECT_TRUE(p.is_degenerate());
}

TEST(ImagePrimitiveDegenerate, NegativeHeightIsDegenerate) {
    ImagePrimitive p{};
    p.height_px = -1.0F;
    EXPECT_TRUE(p.is_degenerate());
}

// ---- ImageOptions equality (default-generated) ----------------------------

TEST(ImageOptionsEquality, DefaultsCompareEqual) {
    EXPECT_EQ(ImageOptions{}, ImageOptions{});
}

TEST(ImageOptionsEquality, DimensionDifferenceCompares) {
    ImageOptions a{};
    ImageOptions b{};
    b.width_px = 300.0F;
    EXPECT_NE(a, b);
}

TEST(ImageOptionsEquality, TintDifferenceCompares) {
    ImageOptions a{};
    ImageOptions b{};
    b.r = 0.0F;
    EXPECT_NE(a, b);
}

// ---- asset_id snapshot ----------------------------------------------------

TEST(ImagePrimitiveAssetId, DefaultIsInvalid) {
    ImagePrimitive p{};
    EXPECT_EQ(p.asset_id, invalid_asset_id);
}

TEST(ImagePrimitiveAssetId, FromOptionsWithoutAssetIdDefaultsToInvalid) {
    const auto p = image_primitive_from(0.0, 0.0, ImageOptions{});
    EXPECT_EQ(p.asset_id, invalid_asset_id);
}

TEST(ImagePrimitiveAssetId, FromOptionsSnapshotsAssetId) {
    const auto p = image_primitive_from(10.0, 20.0, ImageOptions{}, /*asset_id=*/42U);
    EXPECT_EQ(p.asset_id, 42U);
}

TEST(ImagePrimitiveEquality, AssetIdDifferenceCompares) {
    ImagePrimitive a = image_primitive_from(0.0, 0.0, ImageOptions{}, /*asset_id=*/1U);
    ImagePrimitive b = image_primitive_from(0.0, 0.0, ImageOptions{}, /*asset_id=*/2U);
    EXPECT_NE(a, b);
}
