#include "noted/domain/document/image_asset_registry.hpp"

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "noted/domain/document/asset_id.hpp"

namespace {

using noted::domain::AssetId;
using noted::domain::ImageAsset;
using noted::domain::ImageAssetRegistry;
using noted::domain::invalid_asset_id;

[[nodiscard]] auto make_asset(std::string src, std::uint32_t w, std::uint32_t h) -> ImageAsset {
    ImageAsset a{};
    a.source_path = std::move(src);
    a.intrinsic_w_px = w;
    a.intrinsic_h_px = h;
    return a;
}

}  // namespace

// ---- defaults --------------------------------------------------------------

TEST(ImageAssetRegistry, FreshRegistryIsEmpty) {
    ImageAssetRegistry r;
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.size(), 0U);
    EXPECT_TRUE(r.assets().empty());
}

TEST(ImageAssetDefaults, FieldsAreInvalid) {
    ImageAsset a{};
    EXPECT_EQ(a.id, invalid_asset_id);
    EXPECT_TRUE(a.source_path.empty());
    EXPECT_EQ(a.intrinsic_w_px, 0U);
    EXPECT_EQ(a.intrinsic_h_px, 0U);
}

// ---- allocate --------------------------------------------------------------

TEST(ImageAssetRegistryAllocate, FirstIdIsOne) {
    ImageAssetRegistry r;
    const auto id = r.allocate(make_asset("foo.png", 100, 200));
    EXPECT_EQ(id, 1U);
    EXPECT_EQ(r.size(), 1U);
}

TEST(ImageAssetRegistryAllocate, IdsAreMonotonic) {
    ImageAssetRegistry r;
    EXPECT_EQ(r.allocate(make_asset("a.png", 1, 1)), 1U);
    EXPECT_EQ(r.allocate(make_asset("b.png", 1, 1)), 2U);
    EXPECT_EQ(r.allocate(make_asset("c.png", 1, 1)), 3U);
}

TEST(ImageAssetRegistryAllocate, StoresAllFields) {
    ImageAssetRegistry r;
    const auto id = r.allocate(make_asset("path/to/img.png", 640, 480));
    const auto* a = r.find(id);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->id, id);
    EXPECT_EQ(a->source_path, "path/to/img.png");
    EXPECT_EQ(a->intrinsic_w_px, 640U);
    EXPECT_EQ(a->intrinsic_h_px, 480U);
}

// ---- find ------------------------------------------------------------------

TEST(ImageAssetRegistryFind, MissingReturnsNullptr) {
    ImageAssetRegistry r;
    EXPECT_EQ(r.find(1), nullptr);
    EXPECT_EQ(r.find(invalid_asset_id), nullptr);
}

TEST(ImageAssetRegistryFind, LiveReturnsPointer) {
    ImageAssetRegistry r;
    const auto id = r.allocate(make_asset("x.png", 1, 1));
    EXPECT_NE(r.find(id), nullptr);
}

// ---- remove ----------------------------------------------------------------

TEST(ImageAssetRegistryRemove, RemovesLiveAsset) {
    ImageAssetRegistry r;
    const auto id = r.allocate(make_asset("x.png", 1, 1));
    EXPECT_TRUE(r.remove(id));
    EXPECT_EQ(r.find(id), nullptr);
    EXPECT_TRUE(r.empty());
}

TEST(ImageAssetRegistryRemove, MissingReturnsFalse) {
    ImageAssetRegistry r;
    EXPECT_FALSE(r.remove(1));
}

TEST(ImageAssetRegistryRemove, DoesNotRecycleIds) {
    ImageAssetRegistry r;
    const auto id1 = r.allocate(make_asset("a.png", 1, 1));
    EXPECT_TRUE(r.remove(id1));
    const auto id2 = r.allocate(make_asset("b.png", 1, 1));
    EXPECT_GT(id2, id1);  // monotonic across removes
}

// ---- insert (loader path) --------------------------------------------------

TEST(ImageAssetRegistryInsert, RejectsInvalidId) {
    ImageAssetRegistry r;
    ImageAsset a = make_asset("a.png", 1, 1);
    a.id = invalid_asset_id;
    EXPECT_FALSE(r.insert(std::move(a)));
}

TEST(ImageAssetRegistryInsert, RejectsDuplicateId) {
    ImageAssetRegistry r;
    ImageAsset a1 = make_asset("a.png", 1, 1);
    a1.id = 7;
    ASSERT_TRUE(r.insert(a1));

    ImageAsset a2 = make_asset("b.png", 1, 1);
    a2.id = 7;
    EXPECT_FALSE(r.insert(std::move(a2)));
}

TEST(ImageAssetRegistryInsert, AdvancesNextIdPastInsertedValue) {
    ImageAssetRegistry r;
    ImageAsset a = make_asset("a.png", 1, 1);
    a.id = 42;
    ASSERT_TRUE(r.insert(std::move(a)));
    // Subsequent allocate must NOT clash with id=42.
    const auto next = r.allocate(make_asset("b.png", 1, 1));
    EXPECT_EQ(next, 43U);
}

TEST(ImageAssetRegistryInsert, KeepsExistingFields) {
    ImageAssetRegistry r;
    ImageAsset a = make_asset("path", 320, 240);
    a.id = 5;
    ASSERT_TRUE(r.insert(a));
    const auto* loaded = r.find(5);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->source_path, "path");
    EXPECT_EQ(loaded->intrinsic_w_px, 320U);
    EXPECT_EQ(loaded->intrinsic_h_px, 240U);
}

// ---- clear -----------------------------------------------------------------

TEST(ImageAssetRegistryClear, EmptiesAssets) {
    ImageAssetRegistry r;
    (void) r.allocate(make_asset("a.png", 1, 1));
    (void) r.allocate(make_asset("b.png", 1, 1));
    r.clear();
    EXPECT_TRUE(r.empty());
    EXPECT_EQ(r.find(1), nullptr);
}

TEST(ImageAssetRegistryClear, PreservesMonotonicityOfNextId) {
    ImageAssetRegistry r;
    (void) r.allocate(make_asset("a.png", 1, 1));  // id=1
    (void) r.allocate(make_asset("b.png", 1, 1));  // id=2
    r.clear();
    // Next allocate must NOT recycle id=1 or id=2.
    EXPECT_EQ(r.allocate(make_asset("c.png", 1, 1)), 3U);
}

// ---- iteration order -------------------------------------------------------

TEST(ImageAssetRegistryAssets, IterationOrderMatchesInsertion) {
    ImageAssetRegistry r;
    const auto a = r.allocate(make_asset("a.png", 1, 1));
    const auto b = r.allocate(make_asset("b.png", 1, 1));
    const auto c = r.allocate(make_asset("c.png", 1, 1));
    ASSERT_EQ(r.assets().size(), 3U);
    EXPECT_EQ(r.assets()[0].id, a);
    EXPECT_EQ(r.assets()[1].id, b);
    EXPECT_EQ(r.assets()[2].id, c);
}

// ---- equality (default-generated) ------------------------------------------

TEST(ImageAssetRegistryEquality, EmptyRegistriesCompareEqual) {
    EXPECT_EQ(ImageAssetRegistry{}, ImageAssetRegistry{});
}

TEST(ImageAssetRegistryEquality, DifferentAssetsCompareNotEqual) {
    ImageAssetRegistry r1;
    ImageAssetRegistry r2;
    (void) r1.allocate(make_asset("a.png", 1, 1));
    EXPECT_NE(r1, r2);
}
