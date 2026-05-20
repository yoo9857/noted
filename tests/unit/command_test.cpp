#include <memory>
#include <utility>

#include <gtest/gtest.h>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/command/undo_stack.hpp"

namespace {

using noted::canvas::PageBackground;
using noted::domain::AddBlockCommand;
using noted::domain::AddPageCommand;
using noted::domain::BlockId;
using noted::domain::BlockKind;
using noted::domain::Document;
using noted::domain::HeadingPayload;
using noted::domain::InsertBlockCommand;
using noted::domain::invalid_block_id;
using noted::domain::MoveBlockCommand;
using noted::domain::RemoveBlockCommand;
using noted::domain::RemovePageCommand;
using noted::domain::SetNameCommand;
using noted::domain::SetPayloadCommand;
using noted::domain::SetVisibleCommand;
using noted::domain::TextPayload;
using noted::domain::UndoStack;

// Build a small fixture: root group with two text children "a", "b".
struct Fixture {
    Document doc;
    BlockId root{invalid_block_id};
    BlockId a{invalid_block_id};
    BlockId b{invalid_block_id};

    Fixture() {
        root = *doc.add_block(BlockKind::group, invalid_block_id, "root");
        a = *doc.add_block(BlockKind::text, root, "a");
        b = *doc.add_block(BlockKind::text, root, "b");
    }
};

}  // namespace

// ============================================================================
// AddBlockCommand
// ============================================================================

TEST(AddBlockCommand, ApplyAddsBlockAndUndoRemovesIt) {
    Fixture f;
    AddBlockCommand cmd(BlockKind::text, f.root, "c");
    ASSERT_TRUE(cmd.apply(f.doc));
    EXPECT_EQ(f.doc.find(f.root)->children.size(), 3U);
    const auto assigned = cmd.assigned_id();
    ASSERT_NE(assigned, invalid_block_id);
    EXPECT_NE(f.doc.find(assigned), nullptr);
    ASSERT_TRUE(cmd.undo(f.doc));
    EXPECT_EQ(f.doc.find(f.root)->children.size(), 2U);
    EXPECT_EQ(f.doc.find(assigned), nullptr);
}

TEST(AddBlockCommand, RootAllocationAndUndo) {
    Document doc;
    AddBlockCommand cmd(BlockKind::group, invalid_block_id, "root");
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.root(), cmd.assigned_id());
    EXPECT_EQ(doc.size(), 1U);
    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_TRUE(doc.empty());
    EXPECT_EQ(doc.root(), invalid_block_id);
}

TEST(AddBlockCommand, ApplyFailureLeavesDocumentUntouched) {
    Document doc;
    // No root → adding under a non-existent parent fails.
    AddBlockCommand cmd(BlockKind::text, 9999, "stray");
    EXPECT_FALSE(cmd.apply(doc));
    EXPECT_TRUE(doc.empty());
    // Undo should reject because nothing was applied.
    EXPECT_FALSE(cmd.undo(doc));
}

// ============================================================================
// InsertBlockCommand
// ============================================================================

TEST(InsertBlockCommand, InsertsAtIndexAndUndoRemoves) {
    Fixture f;
    InsertBlockCommand cmd(BlockKind::text, f.root, 1, "middle");
    ASSERT_TRUE(cmd.apply(f.doc));
    const auto* root = f.doc.find(f.root);
    ASSERT_EQ(root->children.size(), 3U);
    EXPECT_EQ(root->children[0], f.a);
    EXPECT_EQ(root->children[1], cmd.assigned_id());
    EXPECT_EQ(root->children[2], f.b);
    ASSERT_TRUE(cmd.undo(f.doc));
    EXPECT_EQ(f.doc.find(f.root)->children.size(), 2U);
}

// ============================================================================
// RemoveBlockCommand
// ============================================================================

TEST(RemoveBlockCommand, LeafRoundTrip) {
    Fixture f;
    RemoveBlockCommand cmd(f.a);
    ASSERT_TRUE(cmd.apply(f.doc));
    EXPECT_EQ(f.doc.find(f.a), nullptr);
    EXPECT_EQ(f.doc.find(f.root)->children.size(), 1U);

    ASSERT_TRUE(cmd.undo(f.doc));
    const auto* root = f.doc.find(f.root);
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->children.size(), 2U);
    EXPECT_EQ(root->children[0], f.a);  // restored at index 0
    EXPECT_NE(f.doc.find(f.a), nullptr);
}

TEST(RemoveBlockCommand, SubtreeRoundTripPreservesShape) {
    // root -> group -> {text, text}
    Document doc;
    const auto root = *doc.add_block(BlockKind::group, invalid_block_id);
    const auto g = *doc.add_block(BlockKind::group, root, "g");
    const auto t1 = *doc.add_block(BlockKind::text, g, "t1");
    const auto t2 = *doc.add_block(BlockKind::text, g, "t2");

    RemoveBlockCommand cmd(g);
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.size(), 1U);  // only root left
    EXPECT_EQ(doc.find(g), nullptr);
    EXPECT_EQ(doc.find(t1), nullptr);
    EXPECT_EQ(doc.find(t2), nullptr);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.size(), 4U);
    EXPECT_TRUE(doc.validate());
    const auto* group = doc.find(g);
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->children.size(), 2U);
    EXPECT_EQ(group->children[0], t1);
    EXPECT_EQ(group->children[1], t2);
}

TEST(RemoveBlockCommand, ChildlessRootRoundTrip) {
    Document doc;
    const auto root = *doc.add_block(BlockKind::group, invalid_block_id, "doc");
    RemoveBlockCommand cmd(root);
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_TRUE(doc.empty());
    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.root(), root);
    EXPECT_EQ(doc.find(root)->name, "doc");
}

TEST(RemoveBlockCommand, ApplyFailsOnUnknownTarget) {
    Document doc;
    (void) doc.add_block(BlockKind::group, invalid_block_id);
    RemoveBlockCommand cmd(9999);
    EXPECT_FALSE(cmd.apply(doc));
}

// ============================================================================
// MoveBlockCommand
// ============================================================================

TEST(MoveBlockCommand, ReorderAndUndoRestoresOriginal) {
    // root -> [a, b, c]; move c to index 0; undo back to original.
    Document doc;
    const auto root = *doc.add_block(BlockKind::group, invalid_block_id);
    const auto a = *doc.add_block(BlockKind::text, root);
    const auto b = *doc.add_block(BlockKind::text, root);
    const auto c = *doc.add_block(BlockKind::text, root);

    MoveBlockCommand cmd(c, root, 0);
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.find(root)->children, (std::vector<BlockId>{c, a, b}));
    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.find(root)->children, (std::vector<BlockId>{a, b, c}));
}

TEST(MoveBlockCommand, CrossGroupMoveRoundTrip) {
    Document doc;
    const auto root = *doc.add_block(BlockKind::group, invalid_block_id);
    const auto g1 = *doc.add_block(BlockKind::group, root);
    const auto g2 = *doc.add_block(BlockKind::group, root);
    const auto leaf = *doc.add_block(BlockKind::text, g1);

    MoveBlockCommand cmd(leaf, g2, 0);
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_TRUE(doc.find(g1)->children.empty());
    EXPECT_EQ(doc.find(g2)->children[0], leaf);
    EXPECT_EQ(doc.find(leaf)->parent, g2);
    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.find(g1)->children[0], leaf);
    EXPECT_TRUE(doc.find(g2)->children.empty());
    EXPECT_EQ(doc.find(leaf)->parent, g1);
}

TEST(MoveBlockCommand, ApplyFailsOnRoot) {
    Document doc;
    const auto root = *doc.add_block(BlockKind::group, invalid_block_id);
    const auto g = *doc.add_block(BlockKind::group, root);
    MoveBlockCommand cmd(root, g, 0);
    EXPECT_FALSE(cmd.apply(doc));
}

// ============================================================================
// SetPayloadCommand / SetVisibleCommand / SetNameCommand
// ============================================================================

TEST(SetPayloadCommand, ReplacesAndRestores) {
    Fixture f;
    ASSERT_TRUE(f.doc.set_payload(f.a, TextPayload{"before"}));
    SetPayloadCommand cmd(f.a, TextPayload{"after"});
    ASSERT_TRUE(cmd.apply(f.doc));
    EXPECT_EQ(std::get<TextPayload>(f.doc.find(f.a)->payload).content, "after");
    ASSERT_TRUE(cmd.undo(f.doc));
    EXPECT_EQ(std::get<TextPayload>(f.doc.find(f.a)->payload).content, "before");
}

TEST(SetPayloadCommand, RejectsKindMismatch) {
    Fixture f;
    // a is a text block; image payload is wrong kind.
    SetPayloadCommand cmd(f.a, noted::domain::ImagePayload{});
    EXPECT_FALSE(cmd.apply(f.doc));
}

TEST(SetVisibleCommand, RoundTrip) {
    Fixture f;
    SetVisibleCommand cmd(f.a, false);
    ASSERT_TRUE(cmd.apply(f.doc));
    EXPECT_FALSE(f.doc.find(f.a)->visible);
    ASSERT_TRUE(cmd.undo(f.doc));
    EXPECT_TRUE(f.doc.find(f.a)->visible);
}

TEST(SetNameCommand, RoundTrip) {
    Fixture f;
    SetNameCommand cmd(f.a, "renamed");
    ASSERT_TRUE(cmd.apply(f.doc));
    EXPECT_EQ(f.doc.find(f.a)->name, "renamed");
    ASSERT_TRUE(cmd.undo(f.doc));
    EXPECT_EQ(f.doc.find(f.a)->name, "a");
}

// ============================================================================
// Document::restore_subtree direct tests
// ============================================================================

TEST(RestoreSubtree, RejectsEmptySubtree) {
    Document doc;
    (void) doc.add_block(BlockKind::group, invalid_block_id);
    EXPECT_FALSE(doc.restore_subtree({}, 0));
}

TEST(RestoreSubtree, RejectsAlreadyPresentId) {
    Document doc;
    const auto root = *doc.add_block(BlockKind::group, invalid_block_id);
    // Build a fake subtree using an id that's currently in use.
    std::vector<noted::domain::BlockNode> subtree;
    subtree.push_back(noted::domain::BlockNode{.id = root, .parent = root});
    EXPECT_FALSE(doc.restore_subtree(std::move(subtree), 0));
}

TEST(RestoreSubtree, RootCaseRejectedWhenRootAlreadyExists) {
    Document doc;
    (void) doc.add_block(BlockKind::group, invalid_block_id);
    std::vector<noted::domain::BlockNode> subtree;
    subtree.push_back(noted::domain::BlockNode{.id = 9999, .parent = invalid_block_id});
    EXPECT_FALSE(doc.restore_subtree(std::move(subtree), 0));
}

// ============================================================================
// UndoStack
// ============================================================================

TEST(UndoStack, ExecuteUndoRedo) {
    Fixture f;
    UndoStack stack;
    auto cmd = std::make_unique<SetVisibleCommand>(f.a, false);
    ASSERT_TRUE(stack.execute(std::move(cmd), f.doc));
    EXPECT_FALSE(f.doc.find(f.a)->visible);
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());

    ASSERT_TRUE(stack.undo(f.doc));
    EXPECT_TRUE(f.doc.find(f.a)->visible);
    EXPECT_FALSE(stack.can_undo());
    EXPECT_TRUE(stack.can_redo());

    ASSERT_TRUE(stack.redo(f.doc));
    EXPECT_FALSE(f.doc.find(f.a)->visible);
    EXPECT_TRUE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST(UndoStack, NewExecuteClearsRedoStack) {
    Fixture f;
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetVisibleCommand>(f.a, false), f.doc));
    ASSERT_TRUE(stack.undo(f.doc));
    EXPECT_EQ(stack.redo_size(), 1U);
    // A new edit invalidates the redo path.
    ASSERT_TRUE(stack.execute(std::make_unique<SetNameCommand>(f.a, "x"), f.doc));
    EXPECT_EQ(stack.redo_size(), 0U);
}

TEST(UndoStack, FailedApplyDoesNotPush) {
    Fixture f;
    UndoStack stack;
    // a is text; image payload mismatch will fail.
    auto cmd = std::make_unique<SetPayloadCommand>(f.a, noted::domain::ImagePayload{});
    EXPECT_FALSE(stack.execute(std::move(cmd), f.doc));
    EXPECT_EQ(stack.undo_size(), 0U);
    EXPECT_FALSE(stack.can_undo());
}

TEST(UndoStack, RejectsNullCommand) {
    Document doc;
    UndoStack stack;
    EXPECT_FALSE(stack.execute(nullptr, doc));
}

TEST(UndoStack, UndoWithEmptyStackErrors) {
    Document doc;
    UndoStack stack;
    EXPECT_FALSE(stack.undo(doc));
    EXPECT_FALSE(stack.redo(doc));
}

TEST(UndoStack, DepthBoundTrimsOldest) {
    Fixture f;
    UndoStack stack{3};
    EXPECT_EQ(stack.max_depth(), 3U);
    // Push 5 distinct commands; only the last 3 should remain.
    for (int i = 0; i < 5; ++i) {
        const auto name = std::string{"n"} + std::to_string(i);
        ASSERT_TRUE(stack.execute(std::make_unique<SetNameCommand>(f.a, name), f.doc));
    }
    EXPECT_EQ(stack.undo_size(), 3U);
    EXPECT_EQ(f.doc.find(f.a)->name, "n4");
    // Undo the 3 retained — the earliest 2 names are gone forever.
    ASSERT_TRUE(stack.undo(f.doc));
    EXPECT_EQ(f.doc.find(f.a)->name, "n3");
    ASSERT_TRUE(stack.undo(f.doc));
    EXPECT_EQ(f.doc.find(f.a)->name, "n2");
    ASSERT_TRUE(stack.undo(f.doc));
    // After full unwind, we land at "n1" (the snapshot we couldn't roll back
    // further because the oldest two were trimmed).
    EXPECT_EQ(f.doc.find(f.a)->name, "n1");
    EXPECT_FALSE(stack.can_undo());
}

TEST(UndoStack, ClearWipesBothStacks) {
    Fixture f;
    UndoStack stack;
    ASSERT_TRUE(stack.execute(std::make_unique<SetVisibleCommand>(f.a, false), f.doc));
    ASSERT_TRUE(stack.undo(f.doc));
    stack.clear();
    EXPECT_FALSE(stack.can_undo());
    EXPECT_FALSE(stack.can_redo());
}

TEST(UndoStack, PeekUndoRedoLabels) {
    Fixture f;
    UndoStack stack;
    EXPECT_EQ(stack.peek_undo(), nullptr);
    EXPECT_EQ(stack.peek_redo(), nullptr);
    ASSERT_TRUE(stack.execute(std::make_unique<SetVisibleCommand>(f.a, false), f.doc));
    ASSERT_NE(stack.peek_undo(), nullptr);
    EXPECT_EQ(stack.peek_undo()->label(), "Toggle visibility");
    ASSERT_TRUE(stack.undo(f.doc));
    ASSERT_NE(stack.peek_redo(), nullptr);
    EXPECT_EQ(stack.peek_redo()->label(), "Toggle visibility");
}

TEST(UndoStack, InterleavedExecuteUndoRedoSequence) {
    // Real-world pattern: add A, add B, undo, add C → A and C remain.
    Document doc;
    UndoStack stack;
    ASSERT_TRUE(stack.execute(
        std::make_unique<AddBlockCommand>(BlockKind::group, invalid_block_id, "root"), doc));
    const auto root = doc.root();
    auto a_cmd = std::make_unique<AddBlockCommand>(BlockKind::text, root, "a");
    ASSERT_TRUE(stack.execute(std::move(a_cmd), doc));
    auto b_cmd = std::make_unique<AddBlockCommand>(BlockKind::text, root, "b");
    ASSERT_TRUE(stack.execute(std::move(b_cmd), doc));
    EXPECT_EQ(doc.find(root)->children.size(), 2U);

    ASSERT_TRUE(stack.undo(doc));  // undo add b
    EXPECT_EQ(doc.find(root)->children.size(), 1U);

    auto c_cmd = std::make_unique<AddBlockCommand>(BlockKind::text, root, "c");
    ASSERT_TRUE(stack.execute(std::move(c_cmd), doc));
    EXPECT_EQ(doc.find(root)->children.size(), 2U);
    EXPECT_EQ(doc.find(doc.find(root)->children[1])->name, "c");
    // Redo is invalidated by the new execute.
    EXPECT_FALSE(stack.can_redo());
    EXPECT_TRUE(doc.validate());
}

// ============================================================================
// AddPageCommand
// ============================================================================

TEST(AddPageCommand, ApplyAddsPageAndUndoRemovesIt) {
    Document doc;
    AddPageCommand cmd(612.0F, 792.0F, PageBackground::grid, 50.0F);
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.pages().size(), 1U);
    EXPECT_EQ(cmd.assigned_index(), 0U);
    EXPECT_FLOAT_EQ(doc.pages().pages()[0].extent_w_px, 612.0F);
    EXPECT_EQ(doc.pages().pages()[0].background, PageBackground::grid);
    EXPECT_FLOAT_EQ(doc.pages().pages()[0].origin_x_px, 50.0F);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_TRUE(doc.pages().empty());
}

TEST(AddPageCommand, AssignedIndexFollowsAppendOrder) {
    Document doc;
    AddPageCommand a(100.0F, 100.0F, PageBackground::blank, 0.0F);
    AddPageCommand b(100.0F, 100.0F, PageBackground::lined, 0.0F);
    AddPageCommand c(100.0F, 100.0F, PageBackground::dotted, 0.0F);
    ASSERT_TRUE(a.apply(doc));
    ASSERT_TRUE(b.apply(doc));
    ASSERT_TRUE(c.apply(doc));
    EXPECT_EQ(a.assigned_index(), 0U);
    EXPECT_EQ(b.assigned_index(), 1U);
    EXPECT_EQ(c.assigned_index(), 2U);
}

TEST(AddPageCommand, UndoOnUnappliedRejects) {
    Document doc;
    AddPageCommand cmd(100.0F, 100.0F, PageBackground::blank, 0.0F);
    EXPECT_FALSE(cmd.undo(doc));
}

// ============================================================================
// RemovePageCommand
// ============================================================================

TEST(RemovePageCommand, ApplyRemovesAndUndoRestoresAtSameIndex) {
    Document doc;
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::blank, 10.0F);
    (void) *doc.add_page(200.0F, 300.0F, PageBackground::grid, 20.0F);
    (void) *doc.add_page(400.0F, 500.0F, PageBackground::lined, 30.0F);

    RemovePageCommand cmd(1);
    ASSERT_TRUE(cmd.apply(doc));
    EXPECT_EQ(doc.pages().size(), 2U);
    // After remove, page formerly at 2 slides into index 1.
    EXPECT_EQ(doc.pages().pages()[1].background, PageBackground::lined);

    ASSERT_TRUE(cmd.undo(doc));
    EXPECT_EQ(doc.pages().size(), 3U);
    // Restored at original index 1 with original extent + background + x.
    const auto& restored = doc.pages().pages()[1];
    EXPECT_FLOAT_EQ(restored.extent_w_px, 200.0F);
    EXPECT_FLOAT_EQ(restored.extent_h_px, 300.0F);
    EXPECT_EQ(restored.background, PageBackground::grid);
    EXPECT_FLOAT_EQ(restored.origin_x_px, 20.0F);
}

TEST(RemovePageCommand, ApplyOnOutOfRangeRejects) {
    Document doc;
    (void) *doc.add_page(100.0F, 100.0F, PageBackground::blank, 0.0F);
    RemovePageCommand cmd(99);
    EXPECT_FALSE(cmd.apply(doc));
    EXPECT_EQ(doc.pages().size(), 1U);  // doc untouched
}

TEST(RemovePageCommand, UndoOnUnappliedRejects) {
    Document doc;
    RemovePageCommand cmd(0);
    EXPECT_FALSE(cmd.undo(doc));
}

// ============================================================================
// UndoStack: page commands integrate with the block-command stack
// ============================================================================

TEST(UndoStackPages, MixedBlockAndPageHistoryUndoRedoes) {
    Document doc;
    UndoStack stack;
    (void) *doc.add_block(BlockKind::group, invalid_block_id, "root");

    ASSERT_TRUE(stack.execute(
        std::make_unique<AddPageCommand>(100.0F, 100.0F, PageBackground::grid, 0.0F), doc));
    ASSERT_TRUE(
        stack.execute(std::make_unique<AddBlockCommand>(BlockKind::text, doc.root(), "t"), doc));
    ASSERT_TRUE(stack.execute(
        std::make_unique<AddPageCommand>(100.0F, 100.0F, PageBackground::lined, 0.0F), doc));
    EXPECT_EQ(doc.pages().size(), 2U);
    EXPECT_EQ(doc.find(doc.root())->children.size(), 1U);

    ASSERT_TRUE(stack.undo(doc));  // pages 2 → 1
    ASSERT_TRUE(stack.undo(doc));  // block 1 → 0
    ASSERT_TRUE(stack.undo(doc));  // pages 1 → 0
    EXPECT_EQ(doc.pages().size(), 0U);
    EXPECT_EQ(doc.find(doc.root())->children.size(), 0U);

    ASSERT_TRUE(stack.redo(doc));
    ASSERT_TRUE(stack.redo(doc));
    ASSERT_TRUE(stack.redo(doc));
    EXPECT_EQ(doc.pages().size(), 2U);
    EXPECT_EQ(doc.find(doc.root())->children.size(), 1U);
}
