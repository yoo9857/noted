#include "noted/domain/document/document.hpp"

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using noted::domain::BlockId;
using noted::domain::BlockKind;
using noted::domain::BlockPayload;
using noted::domain::CanvasPayload;
using noted::domain::CodePayload;
using noted::domain::default_payload_for;
using noted::domain::Document;
using noted::domain::EmbedPayload;
using noted::domain::GroupPayload;
using noted::domain::HeadingPayload;
using noted::domain::ImagePayload;
using noted::domain::invalid_block_id;
using noted::domain::kind_of;
using noted::domain::TextPayload;

}  // namespace

// ---- default_payload_for / kind_of round-trip ------------------------------

TEST(BlockPayload, DefaultPayloadKindRoundTrip) {
    constexpr std::array<BlockKind, 7> kAll{
        BlockKind::group,
        BlockKind::text,
        BlockKind::heading,
        BlockKind::code,
        BlockKind::canvas,
        BlockKind::image,
        BlockKind::embed,
    };
    for (const auto k : kAll) {
        EXPECT_EQ(kind_of(default_payload_for(k)), k);
    }
}

// ---- Root allocation -------------------------------------------------------

TEST(DocumentRoot, AddBlockWithInvalidParentAllocatesRoot) {
    Document d;
    auto r = d.add_block(BlockKind::group, invalid_block_id, "doc");
    ASSERT_TRUE(r);
    EXPECT_EQ(d.root(), *r);
    EXPECT_EQ(d.size(), 1U);
    const auto* node = d.find(*r);
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->parent, invalid_block_id);
    EXPECT_EQ(node->kind, BlockKind::group);
    EXPECT_EQ(node->name, "doc");
}

TEST(DocumentRoot, SecondRootAttemptRejected) {
    Document d;
    ASSERT_TRUE(d.add_block(BlockKind::group, invalid_block_id));
    auto r2 = d.add_block(BlockKind::group, invalid_block_id);
    EXPECT_FALSE(r2);
}

TEST(DocumentRoot, AddBlockUnknownParentRejected) {
    Document d;
    auto r = d.add_block(BlockKind::text, 999, "stray");
    EXPECT_FALSE(r);
    EXPECT_EQ(d.size(), 0U);
}

// ---- add / insert ----------------------------------------------------------

TEST(DocumentInsert, AppendInOrder) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::text, root, "a");
    const auto b = *d.add_block(BlockKind::text, root, "b");
    const auto c = *d.add_block(BlockKind::text, root, "c");
    const auto* node = d.find(root);
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->children.size(), 3U);
    EXPECT_EQ(node->children[0], a);
    EXPECT_EQ(node->children[1], b);
    EXPECT_EQ(node->children[2], c);
}

TEST(DocumentInsert, InsertAtIndex) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::text, root);
    const auto c = *d.add_block(BlockKind::text, root);
    const auto b = *d.insert_block(BlockKind::text, root, 1);
    const auto* node = d.find(root);
    ASSERT_EQ(node->children.size(), 3U);
    EXPECT_EQ(node->children[0], a);
    EXPECT_EQ(node->children[1], b);
    EXPECT_EQ(node->children[2], c);
}

TEST(DocumentInsert, IndexOutOfRangeRejected) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    auto r = d.insert_block(BlockKind::text, root, 99);
    EXPECT_FALSE(r);
}

TEST(DocumentInsert, InsertIntoInvalidParentRejected) {
    Document d;
    auto r = d.insert_block(BlockKind::text, invalid_block_id, 0);
    EXPECT_FALSE(r);
}

// ---- remove subtree --------------------------------------------------------

TEST(DocumentRemove, RemoveLeafDetachesFromParent) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto leaf = *d.add_block(BlockKind::text, root);
    EXPECT_EQ(d.size(), 2U);
    EXPECT_TRUE(d.remove_block(leaf));
    EXPECT_EQ(d.size(), 1U);
    const auto* node = d.find(root);
    EXPECT_TRUE(node->children.empty());
    EXPECT_EQ(d.find(leaf), nullptr);
}

TEST(DocumentRemove, RemoveSubtreeRemovesDescendants) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto group_a = *d.add_block(BlockKind::group, root);
    const auto t1 = *d.add_block(BlockKind::text, group_a);
    const auto t2 = *d.add_block(BlockKind::text, group_a);
    EXPECT_EQ(d.size(), 4U);
    EXPECT_TRUE(d.remove_block(group_a));
    EXPECT_EQ(d.size(), 1U);
    EXPECT_EQ(d.find(group_a), nullptr);
    EXPECT_EQ(d.find(t1), nullptr);
    EXPECT_EQ(d.find(t2), nullptr);
}

TEST(DocumentRemove, RemoveRootWithChildrenRejected) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    (void) d.add_block(BlockKind::text, root);
    EXPECT_FALSE(d.remove_block(root));
    EXPECT_EQ(d.size(), 2U);
}

TEST(DocumentRemove, RemoveRootWithoutChildrenSucceeds) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    EXPECT_TRUE(d.remove_block(root));
    EXPECT_EQ(d.root(), invalid_block_id);
    EXPECT_TRUE(d.empty());
}

TEST(DocumentRemove, UnknownIdRejected) {
    Document d;
    (void) d.add_block(BlockKind::group, invalid_block_id);
    EXPECT_FALSE(d.remove_block(9999));
    EXPECT_FALSE(d.remove_block(invalid_block_id));
}

// ---- move_to ---------------------------------------------------------------

TEST(DocumentMove, MoveBetweenGroupsReparents) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto g1 = *d.add_block(BlockKind::group, root);
    const auto g2 = *d.add_block(BlockKind::group, root);
    const auto leaf = *d.add_block(BlockKind::text, g1);

    EXPECT_TRUE(d.move_to(leaf, g2, 0));
    EXPECT_EQ(d.find(leaf)->parent, g2);
    EXPECT_TRUE(d.find(g1)->children.empty());
    ASSERT_EQ(d.find(g2)->children.size(), 1U);
    EXPECT_EQ(d.find(g2)->children[0], leaf);
}

TEST(DocumentMove, MoveToSelfRejected) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::group, root);
    EXPECT_FALSE(d.move_to(a, a, 0));
}

TEST(DocumentMove, MoveToDescendantRejected) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::group, root);
    const auto b = *d.add_block(BlockKind::group, a);
    // Trying to make `a` a child of its own descendant `b` must fail.
    EXPECT_FALSE(d.move_to(a, b, 0));
}

TEST(DocumentMove, MoveRootRejected) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto child = *d.add_block(BlockKind::group, root);
    EXPECT_FALSE(d.move_to(root, child, 0));
}

TEST(DocumentMove, MoveIndexOutOfRangeRejected) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto g = *d.add_block(BlockKind::group, root);
    const auto leaf = *d.add_block(BlockKind::text, root);
    EXPECT_FALSE(d.move_to(leaf, g, 99));
}

TEST(DocumentMove, MoveToCurrentPositionIsNoOp) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::text, root);
    const auto b = *d.add_block(BlockKind::text, root);
    (void) b;
    EXPECT_TRUE(d.move_to(a, root, 0));
    EXPECT_EQ(d.find(root)->children[0], a);
}

// ---- payload set/get -------------------------------------------------------

TEST(DocumentPayload, SetPayloadMustMatchKind) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto t = *d.add_block(BlockKind::text, root);
    // Wrong kind: text node, image payload.
    EXPECT_FALSE(d.set_payload(t, ImagePayload{}));
    // Right kind succeeds.
    EXPECT_TRUE(d.set_payload(t, TextPayload{"hello"}));
    const auto* node = d.find(t);
    ASSERT_NE(node, nullptr);
    const auto* text = std::get_if<TextPayload>(&node->payload);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->content, "hello");
}

TEST(DocumentPayload, HeadingLevelClampedOnSet) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto h = *d.add_block(BlockKind::heading, root);
    ASSERT_TRUE(d.set_payload(h, HeadingPayload{.content = "h", .level = 99}));
    EXPECT_EQ(std::get<HeadingPayload>(d.find(h)->payload).level, 6);
    ASSERT_TRUE(d.set_payload(h, HeadingPayload{.content = "h", .level = 0}));
    EXPECT_EQ(std::get<HeadingPayload>(d.find(h)->payload).level, 1);
}

TEST(DocumentPayload, CanvasAndImageCarrySideStoreIds) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto c = *d.add_block(BlockKind::canvas, root);
    ASSERT_TRUE(d.set_payload(c, CanvasPayload{.graph_id = 42, .width = 800, .height = 600}));
    const auto& cp = std::get<CanvasPayload>(d.find(c)->payload);
    EXPECT_EQ(cp.graph_id, 42U);
    EXPECT_EQ(cp.width, 800U);
    EXPECT_EQ(cp.height, 600U);

    const auto img = *d.add_block(BlockKind::image, root);
    ASSERT_TRUE(d.set_payload(img, ImagePayload{.asset_id = 7, .edit_graph_id = 13}));
    const auto& ip = std::get<ImagePayload>(d.find(img)->payload);
    EXPECT_EQ(ip.asset_id, 7U);
    EXPECT_EQ(ip.edit_graph_id, 13U);
}

TEST(DocumentPayload, SetVisibleAndName) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto t = *d.add_block(BlockKind::text, root, "first");
    EXPECT_TRUE(d.find(t)->visible);
    ASSERT_TRUE(d.set_visible(t, false));
    EXPECT_FALSE(d.find(t)->visible);
    ASSERT_TRUE(d.set_name(t, "renamed"));
    EXPECT_EQ(d.find(t)->name, "renamed");
}

// ---- preorder + traversal --------------------------------------------------

TEST(DocumentPreorder, RootlessDocumentEmptyTraversal) {
    Document d;
    auto r = d.preorder();
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->empty());
}

TEST(DocumentPreorder, PreOrderRespectsChildOrder) {
    // root
    // ├── a
    // │   ├── a1
    // │   └── a2
    // └── b
    //     └── b1
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::group, root);
    const auto a1 = *d.add_block(BlockKind::text, a);
    const auto a2 = *d.add_block(BlockKind::text, a);
    const auto b = *d.add_block(BlockKind::group, root);
    const auto b1 = *d.add_block(BlockKind::text, b);

    auto order = d.preorder();
    ASSERT_TRUE(order);
    const std::vector<BlockId> want{root, a, a1, a2, b, b1};
    EXPECT_EQ(*order, want);
}

TEST(DocumentPreorder, UnknownStartRejected) {
    Document d;
    (void) d.add_block(BlockKind::group, invalid_block_id);
    auto r = d.preorder(9999);
    EXPECT_FALSE(r);
}

// ---- clear -----------------------------------------------------------------

TEST(DocumentClear, WipesEverythingButKeepsNextIdMonotonic) {
    Document d;
    const auto a = *d.add_block(BlockKind::group, invalid_block_id);
    (void) d.add_block(BlockKind::text, a);
    EXPECT_EQ(d.size(), 2U);
    d.clear();
    EXPECT_EQ(d.size(), 0U);
    EXPECT_EQ(d.root(), invalid_block_id);
    // Next allocated ID must be distinct from any previous ID — guards
    // against history references resurfacing on a fresh document.
    const auto fresh = *d.add_block(BlockKind::group, invalid_block_id);
    EXPECT_GT(fresh, a);
}

// ---- validate --------------------------------------------------------------

TEST(DocumentValidate, EmptyDocumentValidates) {
    Document d;
    EXPECT_TRUE(d.validate());
}

TEST(DocumentValidate, HealthyTreeValidates) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto a = *d.add_block(BlockKind::group, root);
    (void) d.add_block(BlockKind::text, a);
    (void) d.add_block(BlockKind::image, root);
    EXPECT_TRUE(d.validate());
}

TEST(DocumentValidate, MoveAndRemovePreserveValidity) {
    Document d;
    const auto root = *d.add_block(BlockKind::group, invalid_block_id);
    const auto g1 = *d.add_block(BlockKind::group, root);
    const auto g2 = *d.add_block(BlockKind::group, root);
    const auto leaf = *d.add_block(BlockKind::text, g1);
    ASSERT_TRUE(d.move_to(leaf, g2, 0));
    EXPECT_TRUE(d.validate());
    ASSERT_TRUE(d.remove_block(g2));  // removes leaf with it
    EXPECT_TRUE(d.validate());
}
