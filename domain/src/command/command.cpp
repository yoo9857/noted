#include <algorithm>
#include <utility>

#include "noted/domain/command/commands.hpp"
#include "noted/domain/command/undo_stack.hpp"

namespace noted::domain {

namespace {

// Pre-order walk of the subtree rooted at `start`, copying each
// BlockNode into `out`. Stable order — guarantees parents precede
// their children, which is the invariant `restore_subtree` requires.
void snapshot_subtree(const Document& doc, BlockId start, std::vector<BlockNode>& out) {
    std::vector<BlockId> stack;
    stack.push_back(start);
    while (!stack.empty()) {
        const auto id = stack.back();
        stack.pop_back();
        const auto* node = doc.find(id);
        if (node == nullptr) {
            continue;
        }
        out.push_back(*node);
        // Push children in reverse so the leftmost child is popped first.
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            stack.push_back(*it);
        }
    }
}

// Locate the index of `id` in its parent's children list. Returns
// (parent_id, index). For the document root returns
// (invalid_block_id, 0).
auto find_position(const Document& doc, BlockId id) -> std::pair<BlockId, std::size_t> {
    const auto* node = doc.find(id);
    if (node == nullptr || node->parent == invalid_block_id) {
        return {invalid_block_id, 0};
    }
    const auto* parent = doc.find(node->parent);
    if (parent == nullptr) {
        return {invalid_block_id, 0};
    }
    const auto it = std::find(parent->children.begin(), parent->children.end(), id);
    if (it == parent->children.end()) {
        return {node->parent, 0};
    }
    return {node->parent, static_cast<std::size_t>(it - parent->children.begin())};
}

}  // namespace

// ============================================================================
// AddBlockCommand
// ============================================================================

AddBlockCommand::AddBlockCommand(BlockKind kind, BlockId parent, std::string name)
    : kind_(kind), parent_(parent), name_(std::move(name)) {}

auto AddBlockCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_block(kind_, parent_, name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = *r;
    was_root_alloc_ = (parent_ == invalid_block_id);
    return {};
}

auto AddBlockCommand::undo(Document& doc) -> Result<void> {
    if (assigned_id_ == invalid_block_id) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddBlockCommand::undo: command was not applied"));
    }
    auto r = doc.remove_block(assigned_id_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = invalid_block_id;
    was_root_alloc_ = false;
    return {};
}

// ============================================================================
// InsertBlockCommand
// ============================================================================

InsertBlockCommand::InsertBlockCommand(BlockKind kind,
                                       BlockId parent,
                                       std::size_t index,
                                       std::string name)
    : kind_(kind), parent_(parent), index_(index), name_(std::move(name)) {}

auto InsertBlockCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.insert_block(kind_, parent_, index_, name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = *r;
    return {};
}

auto InsertBlockCommand::undo(Document& doc) -> Result<void> {
    if (assigned_id_ == invalid_block_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "InsertBlockCommand::undo: command was not applied"));
    }
    auto r = doc.remove_block(assigned_id_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_id_ = invalid_block_id;
    return {};
}

// ============================================================================
// RemoveBlockCommand
// ============================================================================

RemoveBlockCommand::RemoveBlockCommand(BlockId target) : target_(target) {}

auto RemoveBlockCommand::apply(Document& doc) -> Result<void> {
    // Capture position + subtree BEFORE the mutation; if remove fails,
    // we discard the capture and the document is untouched.
    const auto* target = doc.find(target_);
    if (target == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemoveBlockCommand::apply: unknown target " + std::to_string(target_)));
    }
    std::vector<BlockNode> snapshot;
    snapshot_subtree(doc, target_, snapshot);

    const auto [parent_before, index_before] = find_position(doc, target_);

    auto r = doc.remove_block(target_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    snapshot_ = std::move(snapshot);
    old_index_ = index_before;
    (void) parent_before;  // already encoded in snapshot[0].parent
    return {};
}

auto RemoveBlockCommand::undo(Document& doc) -> Result<void> {
    if (snapshot_.empty()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemoveBlockCommand::undo: command was not applied"));
    }
    // Move the snapshot into the document (restore_subtree takes by value).
    auto local = std::move(snapshot_);
    auto r = doc.restore_subtree(std::move(local), old_index_);
    if (!r) {
        // Restoration failed — re-stash the snapshot so a later retry
        // (or the test author) can inspect it.
        snapshot_ = std::move(local);
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// MoveBlockCommand
// ============================================================================

MoveBlockCommand::MoveBlockCommand(BlockId target, BlockId new_parent, std::size_t new_index)
    : target_(target), new_parent_(new_parent), new_index_(new_index) {}

auto MoveBlockCommand::apply(Document& doc) -> Result<void> {
    const auto [parent_before, index_before] = find_position(doc, target_);
    if (parent_before == invalid_block_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "MoveBlockCommand::apply: target is unknown or has no parent (cannot move the root)"));
    }
    auto r = doc.move_to(target_, new_parent_, new_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_parent_ = parent_before;
    old_index_ = index_before;
    return {};
}

auto MoveBlockCommand::undo(Document& doc) -> Result<void> {
    if (old_parent_ == invalid_block_id) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "MoveBlockCommand::undo: command was not applied"));
    }
    auto r = doc.move_to(target_, old_parent_, old_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_parent_ = invalid_block_id;
    return {};
}

// ============================================================================
// SetPayloadCommand
// ============================================================================

SetPayloadCommand::SetPayloadCommand(BlockId target, BlockPayload new_payload)
    : target_(target), new_payload_(std::move(new_payload)) {}

auto SetPayloadCommand::apply(Document& doc) -> Result<void> {
    const auto* node = doc.find(target_);
    if (node == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "SetPayloadCommand::apply: unknown target " + std::to_string(target_)));
    }
    BlockPayload before = node->payload;
    auto r = doc.set_payload(target_, new_payload_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_payload_ = std::move(before);
    return {};
}

auto SetPayloadCommand::undo(Document& doc) -> Result<void> {
    auto r = doc.set_payload(target_, old_payload_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// SetVisibleCommand
// ============================================================================

SetVisibleCommand::SetVisibleCommand(BlockId target, bool visible)
    : target_(target), new_visible_(visible) {}

auto SetVisibleCommand::apply(Document& doc) -> Result<void> {
    const auto* node = doc.find(target_);
    if (node == nullptr) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "SetVisibleCommand::apply: unknown target " + std::to_string(target_)));
    }
    const bool before = node->visible;
    auto r = doc.set_visible(target_, new_visible_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_visible_ = before;
    return {};
}

auto SetVisibleCommand::undo(Document& doc) -> Result<void> {
    auto r = doc.set_visible(target_, old_visible_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// SetNameCommand
// ============================================================================

SetNameCommand::SetNameCommand(BlockId target, std::string new_name)
    : target_(target), new_name_(std::move(new_name)) {}

auto SetNameCommand::apply(Document& doc) -> Result<void> {
    const auto* node = doc.find(target_);
    if (node == nullptr) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_argument,
                              "SetNameCommand::apply: unknown target " + std::to_string(target_)));
    }
    std::string before = node->name;
    auto r = doc.set_name(target_, new_name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    old_name_ = std::move(before);
    return {};
}

auto SetNameCommand::undo(Document& doc) -> Result<void> {
    auto r = doc.set_name(target_, old_name_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    return {};
}

// ============================================================================
// AddPageCommand
// ============================================================================

AddPageCommand::AddPageCommand(float w, float h, noted::canvas::PageBackground bg, float origin_x)
    : w_(w), h_(h), bg_(bg), origin_x_(origin_x) {}

auto AddPageCommand::apply(Document& doc) -> Result<void> {
    auto r = doc.add_page(w_, h_, bg_, origin_x_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    assigned_index_ = *r;
    applied_ = true;
    return {};
}

auto AddPageCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_state,
                                                 "AddPageCommand::undo: command was not applied"));
    }
    auto r = doc.remove_page(assigned_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// RemovePageCommand
// ============================================================================

RemovePageCommand::RemovePageCommand(std::size_t index) : target_index_(index) {}

auto RemovePageCommand::apply(Document& doc) -> Result<void> {
    if (target_index_ >= doc.pages().size()) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_argument,
            "RemovePageCommand::apply: index " + std::to_string(target_index_) +
                " out of range (size " + std::to_string(doc.pages().size()) + ")"));
    }
    // Snapshot BEFORE removal so the inverse has the full state.
    snapshot_ = doc.pages().pages()[target_index_];
    auto r = doc.remove_page(target_index_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = true;
    return {};
}

auto RemovePageCommand::undo(Document& doc) -> Result<void> {
    if (!applied_) {
        return std::unexpected(noted::make_error(
            noted::ErrorCode::invalid_state, "RemovePageCommand::undo: command was not applied"));
    }
    auto r = doc.insert_page(target_index_, snapshot_);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    applied_ = false;
    return {};
}

// ============================================================================
// UndoStack
// ============================================================================

auto UndoStack::execute(std::unique_ptr<Command> cmd, Document& doc) -> Result<void> {
    if (cmd == nullptr) {
        return std::unexpected(noted::make_error(noted::ErrorCode::invalid_argument,
                                                 "UndoStack::execute: cmd is null"));
    }
    auto r = cmd->apply(doc);
    if (!r) {
        return std::unexpected(std::move(r).error());
    }
    undo_stack_.push_back(std::move(cmd));
    redo_stack_.clear();
    // Trim oldest entries beyond the depth bound. erase-front is O(N)
    // but N is bounded by max_depth_; for a 200-deep stack this is
    // cheaper than the linked-list alternative we'd otherwise reach for.
    if (undo_stack_.size() > max_depth_) {
        const auto excess = undo_stack_.size() - max_depth_;
        undo_stack_.erase(undo_stack_.begin(),
                          undo_stack_.begin() + static_cast<std::ptrdiff_t>(excess));
    }
    return {};
}

auto UndoStack::undo(Document& doc) -> Result<void> {
    if (undo_stack_.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state, "UndoStack::undo: nothing to undo"));
    }
    auto cmd = std::move(undo_stack_.back());
    auto r = cmd->undo(doc);
    if (!r) {
        // Inverse failed — re-stash and surface the error. The document
        // is now in an inconsistent state (a real bug, not user error);
        // the caller is responsible for deciding whether to keep going.
        undo_stack_.back() = std::move(cmd);
        return std::unexpected(std::move(r).error());
    }
    undo_stack_.pop_back();
    redo_stack_.push_back(std::move(cmd));
    return {};
}

auto UndoStack::redo(Document& doc) -> Result<void> {
    if (redo_stack_.empty()) {
        return std::unexpected(
            noted::make_error(noted::ErrorCode::invalid_state, "UndoStack::redo: nothing to redo"));
    }
    auto cmd = std::move(redo_stack_.back());
    auto r = cmd->apply(doc);
    if (!r) {
        redo_stack_.back() = std::move(cmd);
        return std::unexpected(std::move(r).error());
    }
    redo_stack_.pop_back();
    undo_stack_.push_back(std::move(cmd));
    return {};
}

void UndoStack::clear() noexcept {
    undo_stack_.clear();
    redo_stack_.clear();
}

auto UndoStack::peek_undo() const noexcept -> const Command* {
    return undo_stack_.empty() ? nullptr : undo_stack_.back().get();
}

auto UndoStack::peek_redo() const noexcept -> const Command* {
    return redo_stack_.empty() ? nullptr : redo_stack_.back().get();
}

}  // namespace noted::domain
