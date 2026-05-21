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

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "noted/domain/command/command.hpp"
#include "noted/domain/document/document.hpp"
#include "noted/engine/canvas/page.hpp"
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

// Append a page at the bottom of the document's PageList. Records
// the assigned index so `undo()` can remove the exact page created.
class AddPageCommand final : public Command {
public:
    AddPageCommand(float w, float h, noted::canvas::PageBackground bg, float origin_x);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Add page"; }

    [[nodiscard]] auto assigned_index() const noexcept -> std::size_t { return assigned_index_; }

private:
    float w_;
    float h_;
    noted::canvas::PageBackground bg_;
    float origin_x_;
    std::size_t assigned_index_{0};
    bool applied_{false};
};

// Remove a page at a given index. Snapshots the page on apply so undo
// can re-insert it at the same index with the same extent + background.
class RemovePageCommand final : public Command {
public:
    explicit RemovePageCommand(std::size_t index);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Remove page"; }

private:
    std::size_t target_index_;
    noted::canvas::Page snapshot_{};
    bool applied_{false};
};

// Append a shape primitive to the document's shapes list. Records
// the assigned index so `undo()` can remove the exact shape created.
//
// The primitive is taken by value at construction time; the snapshot
// is what gets re-applied on redo (no in-flight stylus state
// captured — the handler already committed the primitive before
// building the command).
class AddShapeCommand final : public Command {
public:
    explicit AddShapeCommand(noted::domain::tool::ShapePrimitive shape);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Add shape"; }

    [[nodiscard]] auto assigned_index() const noexcept -> std::size_t { return assigned_index_; }

private:
    noted::domain::tool::ShapePrimitive shape_;
    std::size_t assigned_index_{0};
    bool applied_{false};
};

// Remove a shape at the given index. Snapshots the shape on apply so
// undo can re-insert it at the same index.
class RemoveShapeCommand final : public Command {
public:
    explicit RemoveShapeCommand(std::size_t index);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Remove shape";
    }

private:
    std::size_t target_index_;
    noted::domain::tool::ShapePrimitive snapshot_{};
    bool applied_{false};
};

// Remove a batch of shapes by index — one undo entry for the whole
// batch, the natural shape of the Cut / Delete-selection user
// gesture. Snapshots each removed shape on apply so undo can
// re-insert all of them at their original indices. Removal order
// is descending so shifting indices don't invalidate the
// remaining work; undo inserts ascending so re-insertion lands at
// the recorded indices.
class DeleteShapesCommand final : public Command {
public:
    explicit DeleteShapesCommand(std::vector<std::size_t> indices);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Delete shapes";
    }

private:
    std::vector<std::size_t> target_indices_;
    // Pairs of (original_index, shape) sorted ASC by index — fills
    // on `apply`, drains on `undo`.
    std::vector<std::pair<std::size_t, noted::domain::tool::ShapePrimitive>> snapshots_;
    bool applied_{false};
};

// Append a batch of shapes to the document — one undo entry for
// the Paste gesture. Records the assigned index of the FIRST
// pasted shape so the host can select-the-paste afterward.
class PasteShapesCommand final : public Command {
public:
    explicit PasteShapesCommand(std::vector<noted::domain::tool::ShapePrimitive> shapes);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Paste shapes";
    }

    [[nodiscard]] auto first_assigned_index() const noexcept -> std::size_t {
        return first_assigned_index_;
    }
    [[nodiscard]] auto count() const noexcept -> std::size_t { return shapes_.size(); }

private:
    std::vector<noted::domain::tool::ShapePrimitive> shapes_;
    std::size_t first_assigned_index_{0};
    bool applied_{false};
};

// Append a text primitive to the document's texts list. Same shape
// as `AddShapeCommand`.
class AddTextCommand final : public Command {
public:
    explicit AddTextCommand(noted::domain::tool::TextPrimitive text);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Add text"; }

    [[nodiscard]] auto assigned_index() const noexcept -> std::size_t { return assigned_index_; }

private:
    noted::domain::tool::TextPrimitive text_;
    std::size_t assigned_index_{0};
    bool applied_{false};
};

// Remove a text primitive at the given index.
class RemoveTextCommand final : public Command {
public:
    explicit RemoveTextCommand(std::size_t index);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Remove text"; }

private:
    std::size_t target_index_;
    noted::domain::tool::TextPrimitive snapshot_{};
    bool applied_{false};
};

// Append an image primitive to the document's images list.
class AddImageCommand final : public Command {
public:
    explicit AddImageCommand(noted::domain::tool::ImagePrimitive image);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Add image"; }

    [[nodiscard]] auto assigned_index() const noexcept -> std::size_t { return assigned_index_; }

private:
    noted::domain::tool::ImagePrimitive image_;
    std::size_t assigned_index_{0};
    bool applied_{false};
};

// Remove an image primitive at the given index.
class RemoveImageCommand final : public Command {
public:
    explicit RemoveImageCommand(std::size_t index);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Remove image";
    }

private:
    std::size_t target_index_;
    noted::domain::tool::ImagePrimitive snapshot_{};
    bool applied_{false};
};

// Append a vector-ink stroke to the document's strokes list. Same
// pattern as `AddShapeCommand` / `AddTextCommand` / `AddImageCommand`
// — the snapshot is moved in on construction, applied on `apply`,
// rolled back on `undo`.
class AddStrokeCommand final : public Command {
public:
    explicit AddStrokeCommand(noted::stroke::Stroke stroke);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override { return "Add stroke"; }

    [[nodiscard]] auto assigned_index() const noexcept -> std::size_t { return assigned_index_; }

private:
    noted::stroke::Stroke stroke_;
    std::size_t assigned_index_{0};
    bool applied_{false};
};

// Remove a stroke at the given index. Snapshots the stroke on apply
// so undo can re-insert it in place.
class RemoveStrokeCommand final : public Command {
public:
    explicit RemoveStrokeCommand(std::size_t index);

    [[nodiscard]] auto apply(Document& doc) -> Result<void> override;
    [[nodiscard]] auto undo(Document& doc) -> Result<void> override;
    [[nodiscard]] auto label() const noexcept -> std::string_view override {
        return "Remove stroke";
    }

private:
    std::size_t target_index_;
    noted::stroke::Stroke snapshot_{};
    bool applied_{false};
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
