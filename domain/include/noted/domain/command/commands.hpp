#pragma once

// Concrete `Command` subclasses — one per user-visible mutation on
// `Document`. Each command captures only what's needed to apply +
// invert; full Document snapshots are deliberately avoided so undo
// stacks stay small even on large documents.
//
// Lifecycle of a command:
//   1. Constructor records the user's intent (kind, ids, target
//      values).
//   2. `apply(doc)` performs the mutation and records the "before"
//      state needed to invert it (e.g. SetPayloadCommand stores the
//      old payload). After success the command is `applied_`.
//   3. `undo(doc)` reverses the change using the stored "before"
//      state. Marks the command as not-applied so it can be re-applied
//      via redo.
//
// All commands are non-copyable, non-movable — they live in
// `unique_ptr`s inside the UndoStack.
//
// Rationale: see docs/architecture/0024-command-undo-redo.md.

#include <cstdint>
#include <string>
#include <vector>

#include "noted/domain/command/command.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::domain {

// Append a new block under `parent` (or allocate the root when
// `parent == invalid_block_id`). Records the assigned id so `undo()`
// can remove the exact block created.
class AddBlockCommand final : public Command {
public:
    AddBlockCommand(BlockKind kind, BlockId parent, std::string name);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Add block"; }

    [[nodiscard]] auto assigned_id() const noexcept -> BlockId { return assigned_id_; }

private:
    BlockKind kind_;
    BlockId parent_;
    std::string name_;
    BlockId assigned_id_{invalid_block_id};
    bool was_root_alloc_{false};  // true when this allocated the document root
};

// Insert at a specific index — same as Add but takes an index.
class InsertBlockCommand final : public Command {
public:
    InsertBlockCommand(BlockKind kind, BlockId parent, std::size_t index, std::string name);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Insert block";
    }

    [[nodiscard]] auto assigned_id() const noexcept -> BlockId { return assigned_id_; }

private:
    BlockKind kind_;
    BlockId parent_;
    std::size_t index_;
    std::string name_;
    BlockId assigned_id_{invalid_block_id};
};

// Remove a block + its subtree. On apply we snapshot the entire
// removed subtree (pre-order) so undo can restore it exactly via
// `Document::restore_subtree()`.
class RemoveBlockCommand final : public Command {
public:
    explicit RemoveBlockCommand(BlockId target);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Remove block";
    }

private:
    BlockId target_;
    // Captured during apply for the inverse:
    std::vector<BlockNode> snapshot_;  // pre-order
    std::size_t old_index_{0};         // position in parent's children before removal
};

// Re-parent a block (and its subtree) to a new (parent, index).
// Captures the old (parent, index) on apply.
class MoveBlockCommand final : public Command {
public:
    MoveBlockCommand(BlockId target, BlockId new_parent, std::size_t new_index);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Move block"; }

private:
    BlockId target_;
    BlockId new_parent_;
    std::size_t new_index_;
    BlockId old_parent_{invalid_block_id};
    std::size_t old_index_{0};
};

// Replace the payload of a block. Records the old payload on apply.
class SetPayloadCommand final : public Command {
public:
    SetPayloadCommand(BlockId target, BlockPayload new_payload);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Edit block"; }

private:
    BlockId target_;
    BlockPayload new_payload_;
    BlockPayload old_payload_{TextPayload{}};
};

// Toggle visibility.
class SetVisibleCommand final : public Command {
public:
    SetVisibleCommand(BlockId target, bool visible);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Toggle visibility";
    }

private:
    BlockId target_;
    bool new_visible_;
    bool old_visible_{true};
};

// Rename a block.
class SetNameCommand final : public Command {
public:
    SetNameCommand(BlockId target, std::string new_name);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Rename block";
    }

private:
    BlockId target_;
    std::string new_name_;
    std::string old_name_;
};

}  // namespace noted::domain
