#include "noted/domain/tool/brush_library.hpp"

#include <gtest/gtest.h>

#include "noted/domain/io/brush_library_json.hpp"
#include "noted/domain/tool/brush_preset.hpp"
#include "noted/domain/tool/options.hpp"

namespace {

using noted::domain::tool::apply_preset_to;
using noted::domain::tool::BrushKind;
using noted::domain::tool::BrushLibrary;
using noted::domain::tool::BrushPreset;
using noted::domain::tool::PenOptions;

TEST(BrushLibrary, FreshLibraryIsEmpty) {
    BrushLibrary lib;
    EXPECT_TRUE(lib.empty());
    EXPECT_EQ(lib.size(), 0U);
}

TEST(BrushLibrary, WithBuiltinsHasSevenFactoryPresets) {
    auto lib = BrushLibrary::with_builtins();
    EXPECT_EQ(lib.size(), 7U);
    for (const auto& p : lib.presets()) {
        EXPECT_TRUE(lib.is_builtin(p.id));
        EXPECT_FALSE(p.name.empty());
        EXPECT_GT(p.max_radius_px, p.min_radius_px - 0.01F);  // sanity
    }
    // Spot-check known names so a future rename / reorder is caught
    // by the test.
    EXPECT_NE(lib.find_by_name("Ink Pen"), nullptr);
    EXPECT_NE(lib.find_by_name("Soft Pencil"), nullptr);
    EXPECT_NE(lib.find_by_name("Airbrush"), nullptr);
}

TEST(BrushLibrary, AddAssignsMonotonicIds) {
    BrushLibrary lib;
    BrushPreset a{};
    a.name = "A";
    BrushPreset b{};
    b.name = "B";
    auto id_a = lib.add(a);
    auto id_b = lib.add(b);
    ASSERT_TRUE(id_a && id_b);
    EXPECT_GT(*id_b, *id_a);
    EXPECT_NE(lib.find(*id_a), nullptr);
    EXPECT_EQ(lib.find(*id_a)->name, "A");
    EXPECT_FALSE(lib.is_builtin(*id_a));
}

TEST(BrushLibrary, RemoveReturnsPresetAndDropsIt) {
    BrushLibrary lib;
    auto id = *lib.add(BrushPreset{.id = 0, .name = "Drop"});
    auto removed = lib.remove(id);
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->name, "Drop");
    EXPECT_EQ(lib.find(id), nullptr);
    EXPECT_TRUE(lib.empty());
}

TEST(BrushLibrary, RemoveUnknownRejects) {
    BrushLibrary lib;
    EXPECT_FALSE(lib.remove(9999));
}

TEST(BrushLibrary, UpdateOverwritesFields) {
    BrushLibrary lib;
    auto id = *lib.add(BrushPreset{.id = 0, .name = "Old", .max_radius_px = 5.0F});
    BrushPreset replacement{};
    replacement.name = "New";
    replacement.max_radius_px = 25.0F;
    ASSERT_TRUE(lib.update(id, replacement));
    const auto* found = lib.find(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "New");
    EXPECT_FLOAT_EQ(found->max_radius_px, 25.0F);
    EXPECT_EQ(found->id, id);  // id stays authoritative
}

TEST(BrushLibrary, InsertRequiresNonZeroId) {
    BrushLibrary lib;
    BrushPreset p{};
    p.id = 0;
    p.name = "Bad";
    EXPECT_FALSE(lib.insert(p));
}

TEST(BrushLibrary, InsertRejectsDuplicateId) {
    BrushLibrary lib;
    BrushPreset a{};
    a.id = 42;
    a.name = "A";
    ASSERT_TRUE(lib.insert(a));
    BrushPreset b{};
    b.id = 42;
    b.name = "B";
    EXPECT_FALSE(lib.insert(b));
}

TEST(BrushLibrary, InsertBumpsAllocator) {
    BrushLibrary lib;
    BrushPreset p{};
    p.id = 100;
    p.name = "Hundred";
    ASSERT_TRUE(lib.insert(p));
    auto next = lib.add(BrushPreset{});
    ASSERT_TRUE(next);
    EXPECT_GT(*next, 100U);
}

TEST(BrushLibrary, ApplyPresetCopiesShapeFields) {
    BrushPreset preset{};
    preset.min_radius_px = 4.0F;
    preset.max_radius_px = 18.0F;
    preset.alpha_gamma = 2.2F;
    preset.stabilizer = 0.65F;
    preset.use_preset_color = false;
    preset.r = 0.5F;
    preset.g = 0.5F;
    preset.b = 0.5F;
    preset.a = 0.4F;

    PenOptions opt{};
    opt.r = 0.1F;
    opt.g = 0.2F;
    opt.b = 0.3F;
    apply_preset_to(preset, opt);
    EXPECT_FLOAT_EQ(opt.min_radius_px, 4.0F);
    EXPECT_FLOAT_EQ(opt.max_radius_px, 18.0F);
    EXPECT_FLOAT_EQ(opt.alpha_gamma, 2.2F);
    EXPECT_FLOAT_EQ(opt.stabilizer, 0.65F);
    // use_preset_color=false → r/g/b preserved
    EXPECT_FLOAT_EQ(opt.r, 0.1F);
    EXPECT_FLOAT_EQ(opt.g, 0.2F);
    EXPECT_FLOAT_EQ(opt.b, 0.3F);
    // alpha always taken from preset (deposit opacity is brush-specific)
    EXPECT_FLOAT_EQ(opt.a, 0.4F);
}

TEST(BrushLibrary, ApplyPresetWithColorOverwritesEverything) {
    BrushPreset preset{};
    preset.use_preset_color = true;
    preset.r = 0.9F;
    preset.g = 0.7F;
    preset.b = 0.2F;
    preset.a = 1.0F;
    PenOptions opt{};
    opt.r = 0.1F;
    apply_preset_to(preset, opt);
    EXPECT_FLOAT_EQ(opt.r, 0.9F);
    EXPECT_FLOAT_EQ(opt.g, 0.7F);
    EXPECT_FLOAT_EQ(opt.b, 0.2F);
}

TEST(BrushLibraryJson, UserRoundTripPreservesUserOnly) {
    auto lib = BrushLibrary::with_builtins();
    BrushPreset mine{};
    mine.name = "My Pencil";
    mine.kind = BrushKind::pencil;
    mine.min_radius_px = 1.5F;
    mine.max_radius_px = 8.0F;
    mine.alpha_gamma = 2.0F;
    mine.r = 0.3F;
    mine.g = 0.3F;
    mine.b = 0.3F;
    mine.a = 0.9F;
    mine.stabilizer = 0.7F;
    mine.softness = 0.5F;
    auto mine_id = *lib.add(mine);

    const auto text = noted::domain::io::brush_library_user_to_json(lib);
    // The JSON must contain "My Pencil" but NOT the built-in names —
    // factory presets re-seed on launch, they don't persist.
    EXPECT_NE(text.find("My Pencil"), std::string::npos);
    EXPECT_EQ(text.find("Ink Pen"), std::string::npos);

    // Reload into a fresh built-in library.
    auto reloaded = BrushLibrary::with_builtins();
    ASSERT_TRUE(noted::domain::io::brush_library_user_from_json(text, reloaded));
    EXPECT_EQ(reloaded.size(), 8U);  // 7 factory + 1 user
    const auto* found = reloaded.find(mine_id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "My Pencil");
    EXPECT_FLOAT_EQ(found->min_radius_px, 1.5F);
    EXPECT_FLOAT_EQ(found->stabilizer, 0.7F);
    EXPECT_FALSE(reloaded.is_builtin(mine_id));
}

TEST(BrushLibraryJson, EmptyUserLibraryLoadsCleanly) {
    auto lib = BrushLibrary::with_builtins();
    const auto text = noted::domain::io::brush_library_user_to_json(lib);
    // No user presets — round-trip should be a no-op.
    auto reloaded = BrushLibrary::with_builtins();
    ASSERT_TRUE(noted::domain::io::brush_library_user_from_json(text, reloaded));
    EXPECT_EQ(reloaded.size(), 7U);
}

TEST(BrushLibraryJson, MalformedJsonRejects) {
    BrushLibrary lib = BrushLibrary::with_builtins();
    EXPECT_FALSE(noted::domain::io::brush_library_user_from_json("{garbage", lib));
}

TEST(BrushLibraryJson, UnknownTopKeyRejects) {
    BrushLibrary lib = BrushLibrary::with_builtins();
    EXPECT_FALSE(
        noted::domain::io::brush_library_user_from_json(R"({"version":1,"junk":true})", lib));
}

TEST(BrushLibraryJson, ZeroIdRejects) {
    BrushLibrary lib = BrushLibrary::with_builtins();
    const auto bad = R"({
        "version": 1,
        "presets": [
            {"id": 0, "name": "Bad", "kind": 0,
             "min_r": 1, "max_r": 10, "ag": 1.0, "r": 0, "g": 0, "b": 0, "a": 1,
             "stab": 0.5, "soft": 0.2, "spacing": 0.1, "scatter": 0,
             "angle": 0, "ajitter": 0}
        ]
    })";
    EXPECT_FALSE(noted::domain::io::brush_library_user_from_json(bad, lib));
}

}  // namespace
