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
    EXPECT_NE(text.find("\"version\": 1"), std::string::npos);
    EXPECT_NE(text.find("\"kind\": 0"), std::string::npos);  // group
    EXPECT_NE(text.find("\"kind\": 2"), std::string::npos);  // heading
}
