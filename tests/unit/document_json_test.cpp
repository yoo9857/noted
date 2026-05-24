#include "noted/domain/io/document_json.hpp"

#include <string>

#include <gtest/gtest.h>

#include "noted/domain/document/document.hpp"

namespace {

using noted::domain::BlockKind;
using noted::domain::CanvasPayload;
using noted::domain::CodePayload;
using noted::domain::Document;
using noted::domain::EmbedPayload;
using noted::domain::HeadingPayload;
using noted::domain::ImagePayload;
using noted::domain::invalid_block_id;
using noted::domain::TextPayload;
using noted::domain::io::document_from_json;
using noted::domain::io::document_to_json;
using noted::domain::io::kDocumentJsonVersion;

}  // namespace

// ---- empty + minimal round-trips -------------------------------------------

TEST(DocumentJson, EmptyDocumentRoundTrip) {
    Document src;
    const auto text = document_to_json(src);
    auto loaded = document_from_json(text);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->empty());
    EXPECT_EQ(loaded->root(), invalid_block_id);
}

TEST(DocumentJson, SingleRootRoundTrip) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id, "doc");
    const auto text = document_to_json(src);
    auto loaded = document_from_json(text);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->root(), root);
    ASSERT_NE(loaded->find(root), nullptr);
    EXPECT_EQ(loaded->find(root)->name, "doc");
}

// ---- full payload coverage --------------------------------------------------

TEST(DocumentJson, AllSevenKindsRoundTrip) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id, "root");

    const auto t = *src.add_block(BlockKind::text, root, "t");
    ASSERT_TRUE(src.set_payload(t, TextPayload{"hello world"}));

    const auto h = *src.add_block(BlockKind::heading, root, "h");
    ASSERT_TRUE(src.set_payload(h, HeadingPayload{.content = "Title", .level = 2}));

    const auto c = *src.add_block(BlockKind::code, root, "c");
    ASSERT_TRUE(src.set_payload(c, CodePayload{.content = "int x;", .language = "cpp"}));

    const auto cv = *src.add_block(BlockKind::canvas, root, "cv");
    ASSERT_TRUE(src.set_payload(cv, CanvasPayload{.graph_id = 42, .width = 800, .height = 600}));

    const auto img = *src.add_block(BlockKind::image, root, "img");
    ASSERT_TRUE(src.set_payload(img, ImagePayload{.asset_id = 7, .edit_graph_id = 13}));

    const auto e = *src.add_block(BlockKind::embed, root, "e");
    ASSERT_TRUE(src.set_payload(e, EmbedPayload{.uri = "https://example.com/x"}));

    const auto text = document_to_json(src);
    auto loaded_or = document_from_json(text);
    ASSERT_TRUE(loaded_or);
    const auto& loaded = *loaded_or;
    EXPECT_EQ(loaded.size(), 7U);
    EXPECT_TRUE(loaded.validate());

    EXPECT_EQ(std::get<TextPayload>(loaded.find(t)->payload).content, "hello world");
    EXPECT_EQ(std::get<HeadingPayload>(loaded.find(h)->payload).level, 2);
    EXPECT_EQ(std::get<CodePayload>(loaded.find(c)->payload).language, "cpp");
    EXPECT_EQ(std::get<CanvasPayload>(loaded.find(cv)->payload).width, 800U);
    EXPECT_EQ(std::get<ImagePayload>(loaded.find(img)->payload).asset_id, 7U);
    EXPECT_EQ(std::get<EmbedPayload>(loaded.find(e)->payload).uri, "https://example.com/x");
}

TEST(DocumentJson, DeepTreeRoundTrip) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id);
    const auto a = *src.add_block(BlockKind::group, root);
    const auto b = *src.add_block(BlockKind::group, a);
    const auto leaf = *src.add_block(BlockKind::text, b);
    ASSERT_TRUE(src.set_payload(leaf, TextPayload{"deep"}));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->size(), 4U);
    EXPECT_TRUE(loaded->validate());
    EXPECT_EQ(std::get<TextPayload>(loaded->find(leaf)->payload).content, "deep");
}

TEST(DocumentJson, PreservesIdsAndOrder) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id);
    const auto a = *src.add_block(BlockKind::text, root);
    const auto b = *src.add_block(BlockKind::text, root);
    const auto c = *src.add_block(BlockKind::text, root);

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->root(), root);
    const auto& kids = loaded->find(root)->children;
    ASSERT_EQ(kids.size(), 3U);
    EXPECT_EQ(kids[0], a);
    EXPECT_EQ(kids[1], b);
    EXPECT_EQ(kids[2], c);
}

TEST(DocumentJson, VisibilityFlagSerializes) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id);
    const auto t = *src.add_block(BlockKind::text, root);
    ASSERT_TRUE(src.set_visible(t, false));
    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    EXPECT_FALSE(loaded->find(t)->visible);
}

// ---- rejection paths --------------------------------------------------------

TEST(DocumentJsonReject, MalformedJson) {
    auto r = document_from_json("{this is not json");
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, NonObjectRoot) {
    auto r = document_from_json("[1, 2, 3]");
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, MissingVersion) {
    auto r = document_from_json(R"({"root": 0, "blocks": []})");
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, UnsupportedVersion) {
    const auto bad = std::string{R"({"version": )"} + std::to_string(kDocumentJsonVersion + 99) +
                     R"(, "root": 0, "blocks": []})";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, UnknownTopLevelKey) {
    auto r = document_from_json(R"({"version": 1, "root": 0, "blocks": [], "stowaway": 42})");
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, UnknownBlockKey) {
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {}, "color": "red"}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, UnknownPayloadKey) {
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 1, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {"content": "hi", "extra": "no"}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, InvalidBlockIdZero) {
    const auto bad = R"({
        "version": 1, "root": 0, "blocks": [
            {"id": 0, "kind": 0, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, KindOrdinalOutOfRange) {
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 99, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, EmptyBlocksWithNonZeroRoot) {
    const auto bad = R"({"version": 1, "root": 5, "blocks": []})";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, RootIdMismatch) {
    // blocks[0].id = 1 but top-level root = 5.
    const auto bad = R"({
        "version": 1, "root": 5, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, RootBlockHasNonZeroParent) {
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "", "parent": 99, "children": [],
             "payload": {}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, ParentReferenceNotEarlier) {
    // restore_subtree requires parents to appear before their children
    // in the array. blocks[1] references parent=99 which isn't earlier.
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {}},
            {"id": 2, "kind": 1, "visible": true, "name": "", "parent": 99, "children": [],
             "payload": {"content": ""}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, DuplicateBlockId) {
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {}},
            {"id": 1, "kind": 1, "visible": true, "name": "", "parent": 1, "children": [],
             "payload": {"content": ""}}
        ]
    })";
    auto r = document_from_json(bad);
    EXPECT_FALSE(r);
}

TEST(DocumentJsonReject, GroupPayloadRejectsAnyField) {
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {"foo": "bar"}}
        ]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, MissingPayloadField) {
    // Heading needs both content and level.
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 2, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {"content": "x"}}
        ]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, WrongPayloadFieldType) {
    // Text content should be string, not number.
    const auto bad = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 1, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {"content": 42}}
        ]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

// ---- heading level clamping on load ----------------------------------------

TEST(DocumentJson, HeadingLevelClampedOnLoad) {
    // A file authored externally can sneak in level=10; reader clamps.
    const auto over = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 2, "visible": true, "name": "", "parent": 0, "children": [],
             "payload": {"content": "h", "level": 99}}
        ]
    })";
    auto loaded = document_from_json(over);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(std::get<HeadingPayload>(loaded->find(1)->payload).level, 6);
}

// ---- next_id after load ----------------------------------------------------

TEST(DocumentJson, NextIdMonotonicAfterLoad) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id);
    const auto last = *src.add_block(BlockKind::text, root);
    (void) last;

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    // Adding a fresh block after load must produce an id strictly greater
    // than every existing id — guarantees no collision with history /
    // restored blocks.
    auto fresh = loaded->add_block(BlockKind::text, root);
    ASSERT_TRUE(fresh);
    EXPECT_GT(*fresh, last);
}

// ---- schema sanity for emitted JSON ----------------------------------------

TEST(DocumentJson, EmittedJsonContainsVersionAndKindOrdinal) {
    Document src;
    const auto root = *src.add_block(BlockKind::group, invalid_block_id, "r");
    const auto h = *src.add_block(BlockKind::heading, root);
    (void) h;
    const auto text = document_to_json(src);
    // The output should include the wire-stable version + heading ordinal (2).
    EXPECT_NE(text.find("\"version\": 10"), std::string::npos);
    EXPECT_NE(text.find("\"kind\": 0"), std::string::npos);  // group
    EXPECT_NE(text.find("\"kind\": 2"), std::string::npos);  // heading
    // v2..v10 — writer emits every side-table even when empty.
    EXPECT_NE(text.find("\"pages\""), std::string::npos);
    EXPECT_NE(text.find("\"shapes\""), std::string::npos);
    EXPECT_NE(text.find("\"texts\""), std::string::npos);
    EXPECT_NE(text.find("\"images\""), std::string::npos);
    EXPECT_NE(text.find("\"image_assets\""), std::string::npos);
    EXPECT_NE(text.find("\"strokes\""), std::string::npos);
    EXPECT_NE(text.find("\"canvas_layers\""), std::string::npos);
}

// ---- v1 back-compat --------------------------------------------------------

TEST(DocumentJson, V1FileLoadsWithEmptyPages) {
    // v1 file (no "pages" key) must continue to parse. Loaded doc
    // has an empty page list.
    const auto v1 = R"({
        "version": 1, "root": 1, "blocks": [
            {"id": 1, "kind": 0, "visible": true, "name": "r", "parent": 0, "children": [],
             "payload": {}}
        ]
    })";
    auto loaded = document_from_json(v1);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->size(), 1U);
    EXPECT_TRUE(loaded->pages().empty());
}

// ---- v2 page round-trip ----------------------------------------------------

TEST(DocumentJson, PagesRoundTripWithMixedBackgrounds) {
    using noted::canvas::PageBackground;
    Document src;
    (void) *src.add_block(BlockKind::group, invalid_block_id, "doc");
    ASSERT_TRUE(src.add_page(612.0F, 792.0F, PageBackground::grid, 100.0F));
    ASSERT_TRUE(src.add_page(400.0F, 500.0F, PageBackground::lined, 50.0F));
    ASSERT_TRUE(src.add_page(800.0F, 600.0F, PageBackground::dotted, 0.0F));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->pages().size(), 3U);

    const auto& items = loaded->pages().pages();
    EXPECT_FLOAT_EQ(items[0].extent_w_px, 612.0F);
    EXPECT_FLOAT_EQ(items[0].extent_h_px, 792.0F);
    EXPECT_EQ(items[0].background, PageBackground::grid);
    EXPECT_FLOAT_EQ(items[0].origin_x_px, 100.0F);

    EXPECT_FLOAT_EQ(items[1].extent_w_px, 400.0F);
    EXPECT_EQ(items[1].background, PageBackground::lined);
    EXPECT_FLOAT_EQ(items[1].origin_x_px, 50.0F);

    EXPECT_FLOAT_EQ(items[2].extent_w_px, 800.0F);
    EXPECT_EQ(items[2].background, PageBackground::dotted);
}

TEST(DocumentJson, GapRoundTripsThroughJson) {
    Document src;
    noted::canvas::PageList custom{40.0F};
    custom.add_page(100.0F, 100.0F, noted::canvas::PageBackground::blank);
    custom.add_page(100.0F, 100.0F, noted::canvas::PageBackground::blank);
    src.replace_pages(std::move(custom));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    EXPECT_FLOAT_EQ(loaded->pages().gap_px(), 40.0F);
    // Second page must reflow to 100 + 40 = 140 after load.
    EXPECT_FLOAT_EQ(loaded->pages().pages()[1].origin_y_px, 140.0F);
}

TEST(DocumentJsonReject, PagesUnknownKey) {
    const auto bad = R"({
        "version": 2, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": [], "stowaway": 1}
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, PageItemBgOutOfRange) {
    const auto bad = R"({
        "version": 2, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": [{"w": 1, "h": 1, "bg": 99, "x": 0}]}
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, PagesMissingItems) {
    const auto bad = R"({
        "version": 2, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0}
    })";
    EXPECT_FALSE(document_from_json(bad));
}

// ---- v3 shapes round-trip --------------------------------------------------

TEST(DocumentJson, ShapesRoundTripWithMixedKinds) {
    using noted::domain::tool::ShapeKind;
    Document src;
    noted::domain::tool::ShapePrimitive rect{};
    rect.kind = ShapeKind::rectangle;
    rect.x0 = 10.0;
    rect.y0 = 20.0;
    rect.x1 = 110.0;
    rect.y1 = 70.0;
    rect.stroke_width_px = 2.5F;
    rect.stroke_r = 0.5F;
    rect.stroke_g = 0.25F;
    rect.stroke_b = 0.125F;
    rect.stroke_a = 0.9F;
    ASSERT_TRUE(src.add_shape(rect));

    noted::domain::tool::ShapePrimitive ell{};
    ell.kind = ShapeKind::ellipse;
    ell.x0 = -5.5;
    ell.y0 = -7.5;
    ell.x1 = 100.5;
    ell.y1 = 50.5;
    ell.stroke_width_px = 4.0F;
    ell.stroke_a = 1.0F;
    ASSERT_TRUE(src.add_shape(ell));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->shapes().size(), 2U);

    const auto& got = loaded->shapes();
    EXPECT_EQ(got[0].kind, ShapeKind::rectangle);
    EXPECT_DOUBLE_EQ(got[0].x0, 10.0);
    EXPECT_DOUBLE_EQ(got[0].x1, 110.0);
    EXPECT_FLOAT_EQ(got[0].stroke_width_px, 2.5F);
    EXPECT_FLOAT_EQ(got[0].stroke_r, 0.5F);
    EXPECT_FLOAT_EQ(got[0].stroke_a, 0.9F);

    EXPECT_EQ(got[1].kind, ShapeKind::ellipse);
    EXPECT_DOUBLE_EQ(got[1].x0, -5.5);
    EXPECT_DOUBLE_EQ(got[1].y1, 50.5);
    EXPECT_FLOAT_EQ(got[1].stroke_width_px, 4.0F);
}

TEST(DocumentJson, V1FileLoadsWithEmptyShapes) {
    // v1 has no shapes field; loader treats absent as empty.
    const auto v1 = R"({
        "version": 1, "root": 0, "blocks": []
    })";
    auto loaded = document_from_json(v1);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->shapes().empty());
}

TEST(DocumentJson, V2FileLoadsWithEmptyShapes) {
    const auto v2 = R"({
        "version": 2, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []}
    })";
    auto loaded = document_from_json(v2);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->shapes().empty());
}

TEST(DocumentJsonReject, ShapeUnknownKey) {
    const auto bad = R"({
        "version": 3, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [{"k": 0, "x0": 0, "y0": 0, "x1": 1, "y1": 1,
                    "sw": 1, "r": 0, "g": 0, "b": 0, "a": 1, "extra": true}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ShapeKindOutOfRange) {
    const auto bad = R"({
        "version": 3, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [{"k": 99, "x0": 0, "y0": 0, "x1": 1, "y1": 1,
                    "sw": 1, "r": 0, "g": 0, "b": 0, "a": 1}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ShapeMissingRequiredKey) {
    const auto bad = R"({
        "version": 3, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [{"k": 0, "x0": 0, "y0": 0, "x1": 1, "y1": 1}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

// ---- v4 texts round-trip ---------------------------------------------------

TEST(DocumentJson, TextsRoundTripPreservesContentAndStyle) {
    Document src;
    noted::domain::tool::TextPrimitive a{};
    a.x = 50.0;
    a.y = 80.0;
    a.content = "Hello, world!";
    a.font_size_px = 24.0F;
    a.r = 0.5F;
    a.g = 0.0F;
    a.b = 0.5F;
    a.a = 1.0F;
    ASSERT_TRUE(src.add_text(a));

    noted::domain::tool::TextPrimitive b{};
    b.x = -10.5;
    b.y = 200.5;
    b.content = "\xED\x95\x9C\xEA\xB8\x80 / \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E / mixed";
    b.font_size_px = 16.0F;
    b.r = 0.1F;
    b.g = 0.2F;
    b.b = 0.3F;
    b.a = 0.5F;
    ASSERT_TRUE(src.add_text(b));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->texts().size(), 2U);
    EXPECT_DOUBLE_EQ(loaded->texts()[0].x, 50.0);
    EXPECT_EQ(loaded->texts()[0].content, "Hello, world!");
    EXPECT_FLOAT_EQ(loaded->texts()[0].font_size_px, 24.0F);
    EXPECT_FLOAT_EQ(loaded->texts()[0].r, 0.5F);

    EXPECT_DOUBLE_EQ(loaded->texts()[1].x, -10.5);
    EXPECT_EQ(loaded->texts()[1].content,
              "\xED\x95\x9C\xEA\xB8\x80 / \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E / mixed");
    EXPECT_FLOAT_EQ(loaded->texts()[1].font_size_px, 16.0F);
}

TEST(DocumentJson, V3FileLoadsWithEmptyTexts) {
    const auto v3 = R"({
        "version": 3, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": []
    })";
    auto loaded = document_from_json(v3);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->texts().empty());
}

TEST(DocumentJson, V4LoaderClampsCorruptFontSize) {
    // A hostile / corrupted file with a negative font size must not
    // smuggle a degenerate primitive past the loader.
    const auto bad_fs = R"({
        "version": 4, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [],
        "texts": [{"x": 0, "y": 0, "s": "hi", "fs": -7,
                   "r": 0, "g": 0, "b": 0, "a": 1}]
    })";
    auto loaded = document_from_json(bad_fs);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->texts().size(), 1U);
    EXPECT_FLOAT_EQ(loaded->texts()[0].font_size_px, 1.0F);
}

TEST(DocumentJsonReject, TextUnknownKey) {
    const auto bad = R"({
        "version": 4, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [],
        "texts": [{"x": 0, "y": 0, "s": "", "fs": 16,
                   "r": 0, "g": 0, "b": 0, "a": 1, "extra": true}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, TextMissingRequiredKey) {
    const auto bad = R"({
        "version": 4, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [],
        "texts": [{"x": 0, "y": 0, "s": ""}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

// ---- v5 images round-trip --------------------------------------------------

TEST(DocumentJson, ImagesRoundTripPreservesGeometryAndTint) {
    Document src;
    noted::domain::tool::ImagePrimitive a{};
    a.x = 10.0;
    a.y = 20.0;
    a.width_px = 300.0F;
    a.height_px = 200.0F;
    a.r = 0.9F;
    a.g = 0.5F;
    a.b = 0.1F;
    a.a = 1.0F;
    ASSERT_TRUE(src.add_image(a));

    noted::domain::tool::ImagePrimitive b{};
    b.x = -5.5;
    b.y = 100.5;
    b.width_px = 50.0F;
    b.height_px = 75.0F;
    b.r = 0.0F;
    b.g = 0.0F;
    b.b = 0.0F;
    b.a = 0.5F;
    ASSERT_TRUE(src.add_image(b));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->images().size(), 2U);
    EXPECT_DOUBLE_EQ(loaded->images()[0].x, 10.0);
    EXPECT_FLOAT_EQ(loaded->images()[0].width_px, 300.0F);
    EXPECT_FLOAT_EQ(loaded->images()[0].r, 0.9F);
    EXPECT_DOUBLE_EQ(loaded->images()[1].x, -5.5);
    EXPECT_FLOAT_EQ(loaded->images()[1].a, 0.5F);
}

TEST(DocumentJson, V4FileLoadsWithEmptyImages) {
    const auto v4 = R"({
        "version": 4, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [],
        "texts": []
    })";
    auto loaded = document_from_json(v4);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->images().empty());
}

TEST(DocumentJson, V5LoaderClampsCorruptDimensions) {
    const auto bad_dims = R"({
        "version": 5, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [],
        "images": [{"x": 0, "y": 0, "w": -50, "h": 0,
                    "r": 1, "g": 1, "b": 1, "a": 1}]
    })";
    auto loaded = document_from_json(bad_dims);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->images().size(), 1U);
    EXPECT_FLOAT_EQ(loaded->images()[0].width_px, 1.0F);
    EXPECT_FLOAT_EQ(loaded->images()[0].height_px, 1.0F);
}

TEST(DocumentJsonReject, ImageUnknownKey) {
    const auto bad = R"({
        "version": 5, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [],
        "images": [{"x": 0, "y": 0, "w": 1, "h": 1,
                    "r": 0, "g": 0, "b": 0, "a": 1, "extra": true}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ImageMissingRequiredKey) {
    const auto bad = R"({
        "version": 5, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [],
        "images": [{"x": 0, "y": 0, "w": 1}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

// ---- v6 image_assets round-trip + back-compat ------------------------------

TEST(DocumentJson, V6ImageAssetRoundTrip) {
    Document src;
    const auto id =
        src.image_assets_mut().allocate(noted::domain::ImageAsset{.id = 0,
                                                                  .source_path = "C:/img/photo.png",
                                                                  .intrinsic_w_px = 1920,
                                                                  .intrinsic_h_px = 1080});

    noted::domain::tool::ImagePrimitive im{};
    im.x = 7.0;
    im.y = 9.0;
    im.width_px = 50.0F;
    im.height_px = 30.0F;
    im.r = 1.0F;
    im.g = 1.0F;
    im.b = 1.0F;
    im.a = 1.0F;
    im.asset_id = id;
    ASSERT_TRUE(src.add_image(im));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->image_assets().size(), 1U);
    const auto* round = loaded->image_assets().find(id);
    ASSERT_NE(round, nullptr);
    EXPECT_EQ(round->source_path, "C:/img/photo.png");
    EXPECT_EQ(round->intrinsic_w_px, 1920U);
    EXPECT_EQ(round->intrinsic_h_px, 1080U);

    ASSERT_EQ(loaded->images().size(), 1U);
    EXPECT_EQ(loaded->images()[0].asset_id, id);
}

TEST(DocumentJson, V5FileLoadsWithEmptyRegistryAndInvalidAssetIds) {
    // No "image_assets" key, image item has no "aid" key — both
    // optional in v5. Loader must populate invalid_asset_id + an
    // empty registry.
    const auto v5 = R"({
        "version": 5, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [],
        "images": [{"x": 1, "y": 2, "w": 3, "h": 4,
                    "r": 1, "g": 1, "b": 1, "a": 1}]
    })";
    auto loaded = document_from_json(v5);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->image_assets().empty());
    ASSERT_EQ(loaded->images().size(), 1U);
    EXPECT_EQ(loaded->images()[0].asset_id, noted::domain::invalid_asset_id);
}

TEST(DocumentJsonReject, ImageAssetUnknownKey) {
    const auto bad = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [],
        "image_assets": [{"id": 1, "src": "x", "iw": 0, "ih": 0, "extra": true}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ImageAssetMissingRequiredKey) {
    const auto bad = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [],
        "image_assets": [{"id": 1, "src": "x"}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ImageAssetDuplicateId) {
    const auto bad = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [],
        "image_assets": [
            {"id": 1, "src": "a", "iw": 0, "ih": 0},
            {"id": 1, "src": "b", "iw": 0, "ih": 0}
        ]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ImageAssetIdZero) {
    const auto bad = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [],
        "image_assets": [{"id": 0, "src": "x", "iw": 0, "ih": 0}]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, ImageAidWithoutMatchingAsset) {
    // image references aid=5 but the registry doesn't contain it.
    const auto bad = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [],
        "images": [{"x": 0, "y": 0, "w": 1, "h": 1,
                    "r": 1, "g": 1, "b": 1, "a": 1, "aid": 5}],
        "image_assets": []
    })";
    auto loaded = document_from_json(bad);
    EXPECT_FALSE(loaded);
}

TEST(DocumentJson, ImageAidZeroAcceptedWithEmptyRegistry) {
    // aid == invalid_asset_id (0) is the placeholder sentinel —
    // referential integrity does NOT require an entry for it.
    const auto ok = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [],
        "images": [{"x": 0, "y": 0, "w": 1, "h": 1,
                    "r": 1, "g": 1, "b": 1, "a": 1, "aid": 0}],
        "image_assets": []
    })";
    auto loaded = document_from_json(ok);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->images().size(), 1U);
    EXPECT_EQ(loaded->images()[0].asset_id, noted::domain::invalid_asset_id);
}

// ---- v7 strokes round-trip + back-compat ----------------------------------

TEST(DocumentJson, V7StrokesRoundTrip) {
    Document src;
    noted::stroke::Stroke s{};
    s.samples = {{.x = 10.0F, .y = 20.0F, .pressure = 1.0F},
                 {.x = 30.0F, .y = 40.0F, .pressure = 0.5F},
                 {.x = 50.0F, .y = 60.0F, .pressure = 0.8F}};
    s.style.min_radius_px = 3.0F;
    s.style.max_radius_px = 12.0F;
    s.style.softness_ratio = 0.15F;
    s.style.alpha_gamma = 1.5F;
    s.style.r = 0.7F;
    s.style.g = 0.3F;
    s.style.b = 0.1F;
    s.style.a = 0.9F;
    s.style.stabilizer = 0.4F;
    s.mode = noted::stroke::DrawMode::erase;
    ASSERT_TRUE(src.add_stroke(s));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    const auto& g = loaded->strokes()[0];
    ASSERT_EQ(g.samples.size(), 3U);
    EXPECT_FLOAT_EQ(g.samples[0].x, 10.0F);
    EXPECT_FLOAT_EQ(g.samples[1].pressure, 0.5F);
    EXPECT_FLOAT_EQ(g.samples[2].y, 60.0F);
    EXPECT_FLOAT_EQ(g.style.min_radius_px, 3.0F);
    EXPECT_FLOAT_EQ(g.style.max_radius_px, 12.0F);
    EXPECT_FLOAT_EQ(g.style.softness_ratio, 0.15F);
    EXPECT_FLOAT_EQ(g.style.alpha_gamma, 1.5F);
    EXPECT_FLOAT_EQ(g.style.r, 0.7F);
    EXPECT_FLOAT_EQ(g.style.a, 0.9F);
    EXPECT_FLOAT_EQ(g.style.stabilizer, 0.4F);
    EXPECT_EQ(g.mode, noted::stroke::DrawMode::erase);
}

TEST(DocumentJson, V6FileLoadsWithEmptyStrokes) {
    const auto v6 = R"({
        "version": 6, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [],
        "image_assets": []
    })";
    auto loaded = document_from_json(v6);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(loaded->strokes().empty());
}

TEST(DocumentJsonReject, StrokesSamplesLengthNotMultipleOf3) {
    const auto bad = R"({
        "version": 7, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [1.0, 2.0],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5}
        }]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, StrokesModeOutOfRange) {
    const auto bad = R"({
        "version": 7, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 99,
            "samples": [0, 0, 1],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5}
        }]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

// ---- v9 pen-dynamics round-trip + back-compat -----------------------------

TEST(DocumentJson, V9StrokesPenDynamicsRoundTrip) {
    // Stroke with all pen-dynamics fields populated: per-sample t /
    // tilt, custom PressureCurve handles, non-zero velocity_blend /
    // tilt_blend. Survives a v9 writer → v9 reader round-trip
    // bit-for-bit on every new field.
    Document src;
    noted::stroke::Stroke s{};
    s.samples = {
        {.x = 10.0F, .y = 20.0F, .pressure = 1.0F, .t = 0.0F, .tilt_x = 0.30F, .tilt_y = -0.10F},
        {.x = 30.0F, .y = 40.0F, .pressure = 0.5F, .t = 0.016F, .tilt_x = 0.25F, .tilt_y = -0.05F},
        {.x = 50.0F, .y = 60.0F, .pressure = 0.8F, .t = 0.033F, .tilt_x = 0.20F, .tilt_y = 0.00F}};
    s.style.min_radius_px = 3.0F;
    s.style.max_radius_px = 12.0F;
    s.style.softness_ratio = 0.15F;
    s.style.alpha_gamma = 1.5F;
    s.style.pressure_curve =
        noted::stroke::PressureCurve{.h1_x = 0.10F, .h1_y = 0.42F, .h2_x = 0.80F, .h2_y = 0.85F};
    s.style.velocity_blend = 0.40F;
    s.style.tilt_blend = 0.70F;
    s.style.r = 0.7F;
    s.style.g = 0.3F;
    s.style.b = 0.1F;
    s.style.a = 0.9F;
    s.style.stabilizer = 0.4F;
    s.mode = noted::stroke::DrawMode::draw;
    ASSERT_TRUE(src.add_stroke(s));

    auto loaded = document_from_json(document_to_json(src));
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    const auto& g = loaded->strokes()[0];
    ASSERT_EQ(g.samples.size(), 3U);
    EXPECT_FLOAT_EQ(g.samples[0].t, 0.0F);
    EXPECT_FLOAT_EQ(g.samples[0].tilt_x, 0.30F);
    EXPECT_FLOAT_EQ(g.samples[0].tilt_y, -0.10F);
    EXPECT_FLOAT_EQ(g.samples[1].t, 0.016F);
    EXPECT_FLOAT_EQ(g.samples[1].tilt_x, 0.25F);
    EXPECT_FLOAT_EQ(g.samples[2].t, 0.033F);
    EXPECT_FLOAT_EQ(g.samples[2].tilt_y, 0.00F);
    EXPECT_FLOAT_EQ(g.style.pressure_curve.h1_x, 0.10F);
    EXPECT_FLOAT_EQ(g.style.pressure_curve.h1_y, 0.42F);
    EXPECT_FLOAT_EQ(g.style.pressure_curve.h2_x, 0.80F);
    EXPECT_FLOAT_EQ(g.style.pressure_curve.h2_y, 0.85F);
    EXPECT_FLOAT_EQ(g.style.velocity_blend, 0.40F);
    EXPECT_FLOAT_EQ(g.style.tilt_blend, 0.70F);
}

TEST(DocumentJson, V8FileMigratesPenDynamicsFromAlphaGamma) {
    // Legacy v8 file: stride-3 samples, no pc / vb / tb fields. v9
    // reader synthesises `pressure_curve` from `ag` via
    // `from_gamma`, defaults vb / tb to 0, and zero-fills per-sample
    // t / tilt. The migrated curve must match what `from_gamma`
    // would produce in production so pen feel survives the upgrade.
    const auto v8 = R"({
        "version": 8, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [0, 0, 1, 10, 20, 0.5],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5}
        }]
    })";
    auto loaded = document_from_json(v8);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    const auto& s = loaded->strokes()[0];
    ASSERT_EQ(s.samples.size(), 2U);
    EXPECT_FLOAT_EQ(s.samples[0].t, 0.0F);
    EXPECT_FLOAT_EQ(s.samples[0].tilt_x, 0.0F);
    EXPECT_FLOAT_EQ(s.samples[0].tilt_y, 0.0F);
    EXPECT_FLOAT_EQ(s.style.velocity_blend, 0.0F);
    EXPECT_FLOAT_EQ(s.style.tilt_blend, 0.0F);
    const auto expected = noted::stroke::PressureCurve::from_gamma(1.8F);
    EXPECT_FLOAT_EQ(s.style.pressure_curve.h1_x, expected.h1_x);
    EXPECT_FLOAT_EQ(s.style.pressure_curve.h1_y, expected.h1_y);
    EXPECT_FLOAT_EQ(s.style.pressure_curve.h2_x, expected.h2_x);
    EXPECT_FLOAT_EQ(s.style.pressure_curve.h2_y, expected.h2_y);
}

TEST(DocumentJsonReject, V9SamplesLengthNotMultipleOf6) {
    const auto bad = R"({
        "version": 9, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [0, 0, 1, 0, 0],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5}
        }]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJson, V9PressureCurveHandlesClampedToUnitSquare) {
    // Out-of-range handles must clamp at the data boundary so a
    // corrupted file can't poison the tessellator's [0, 1] alpha
    // assumption.
    const auto bad_pc = R"({
        "version": 9, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [0, 0, 1, 0, 0, 0],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5,
                      "pc": {"h1x": -1.5, "h1y": 2.0, "h2x": 0.5, "h2y": 0.7},
                      "vb": 0.0, "tb": 0.0}
        }]
    })";
    auto loaded = document_from_json(bad_pc);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    const auto& s = loaded->strokes()[0];
    EXPECT_GE(s.style.pressure_curve.h1_x, 0.0F);
    EXPECT_LE(s.style.pressure_curve.h1_x, 1.0F);
    EXPECT_GE(s.style.pressure_curve.h1_y, 0.0F);
    EXPECT_LE(s.style.pressure_curve.h1_y, 1.0F);
    EXPECT_FLOAT_EQ(s.style.pressure_curve.h2_x, 0.5F);
    EXPECT_FLOAT_EQ(s.style.pressure_curve.h2_y, 0.7F);
}

TEST(DocumentJson, V9VelocityAndTiltBlendClampedToUnit) {
    const auto bad_blends = R"({
        "version": 9, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [0, 0, 1, 0, 0, 0],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5,
                      "vb": 5.0, "tb": -2.0}
        }]
    })";
    auto loaded = document_from_json(bad_blends);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    const auto& s = loaded->strokes()[0];
    EXPECT_FLOAT_EQ(s.style.velocity_blend, 1.0F);
    EXPECT_FLOAT_EQ(s.style.tilt_blend, 0.0F);
}

TEST(DocumentJsonReject, V9PressureCurveUnknownKey) {
    // Strict-mode key check applies to the pc sub-object too. A
    // typo'd key catches the writer's mistake immediately instead of
    // silently dropping the data on round-trip.
    const auto bad = R"({
        "version": 9, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [0, 0, 1, 0, 0, 0],
            "style": {"min_r": 2, "max_r": 10, "soft": 0.2, "ag": 1.8,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 0.5,
                      "pc": {"h1x": 0.3, "h1y": 0.3, "h2x": 0.6, "h2y": 0.6,
                             "bogus": 1.0}}
        }]
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJsonReject, UnsupportedVersion11) {
    // Future versions must be rejected — silently truncating unknown
    // fields would risk data loss on round-trip.
    const auto bad = R"({
        "version": 11, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []}
    })";
    EXPECT_FALSE(document_from_json(bad));
}

TEST(DocumentJson, V7LoaderClampsCorruptStyle) {
    // Negative radii / non-positive gamma get clamped at the boundary
    // so a hostile file can't poison the renderer.
    const auto bad_style = R"({
        "version": 7, "root": 0, "blocks": [],
        "pages": {"gap_px": 0.0, "items": []},
        "shapes": [], "texts": [], "images": [], "image_assets": [],
        "strokes": [{
            "mode": 0,
            "samples": [0, 0, 1, 10, 0, 1],
            "style": {"min_r": -5, "max_r": -1, "soft": 0.2, "ag": -2.0,
                      "r": 0, "g": 0, "b": 0, "a": 1, "stab": 2.0}
        }]
    })";
    auto loaded = document_from_json(bad_style);
    ASSERT_TRUE(loaded);
    ASSERT_EQ(loaded->strokes().size(), 1U);
    const auto& s = loaded->strokes()[0];
    EXPECT_GE(s.style.min_radius_px, 0.5F);
    EXPECT_GE(s.style.max_radius_px, s.style.min_radius_px);
    EXPECT_GT(s.style.alpha_gamma, 0.0F);
    EXPECT_LE(s.style.stabilizer, 0.95F);
}
