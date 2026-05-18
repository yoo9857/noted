#pragma once

// Undo / redo stack over a `Document`.
//
// Owns two stacks of commands:
//   - `undo_stack_`: commands that have been applied and are
//     candidates for rewind.
//   - `redo_stack_`: commands that have been undone and are
//     candidates for re-apply.
//
// The four state-changing operations:
//   - `execute(cmd)`: apply the command to the doc; on success push
//     onto undo_stack_, clear redo_stack_ (because a new edit
//     invalidates any redo path).
//   - `undo()`: pop from undo_stack_, run undo(), push onto redo_stack_.
//   - `redo()`: pop from redo_stack_, re-apply, push onto undo_stack_.
//   - `clear()`: drop both stacks (e.g. on document close).
//
// Bounded depth: when `undo_stack_.size() > max_depth_` after a
// successful execute, the oldest entry is dropped. Keeps memory
// bounded on very long sessions. Default 200 (typical IDE / editor
// undo depth); configurable via the constructor.
//
// Atomicity:
//   - `execute` only mutates state when `apply` returns success.
//   - `undo` / `redo` move the command between stacks **only** when
//     the inverse / re-apply succeeds. On failure (which signals a
//     bug — the command was supposed to be a precise inverse), the
//     stack is left untouched so a debugger can inspect what happened.
//
// Pure logic — no threading, no I/O. The host is responsible for
// serialization (this matches the rest of the codebase: the document
// is single-writer by design).
//
// Rationale: see docs/architecture/0024-command-undo-redo.md.

#include <cstddef>
#include <memory>
#include <vector>

#include "noted/domain/command/command.hpp"
#include "noted/engine/error/error.hpp"

namespace noted::domain {

class Document;

class UndoStack {
public:
    static constexpr std::size_t kDefaultMaxDepth = 200;

    UndoStack() = default;
    explicit UndoStack(std::size_t max_depth) noexcept
        : max_depth_(max_depth == 0 ? kDefaultMaxDepth : max_depth) {}

    UndoStack(const UndoStack&) = delete;
    auto operator=(const UndoStack&) -> UndoStack& = delete;
    UndoStack(UndoStack&&) noexcept = default;
    auto operator=(UndoStack&&) noexcept -> UndoStack& = default;
    ~UndoStack() = default;

    // Apply `cmd` to `doc`. On success, the command is owned by the
    // stack; on failure, the command is returned to the caller via the
    // `error`-laden Result so it can be retried or inspected.
    //
    // Side-effects of success: redo_stack_ is cleared (new edit
    // invalidates any redo path); oldest undo entry trimmed if depth
    // exceeded.
    [[nodiscard]] auto execute(std::unique_ptr<Command> cmd, Document& doc) -> Result<void>;

    // Pop from undo_stack_, undo on the doc, push onto redo_stack_.
    // Errors when there's nothing to undo, or when the command's
    // undo() returns failure (signals a bug).
    [[nodiscard]] auto undo(Document& doc) -> Result<void>;

    // Pop from redo_stack_, re-apply, push onto undo_stack_.
    [[nodiscard]] auto redo(Document& doc) -> Result<void>;

    void clear() noexcept;

    [[nodiscard]] auto can_undo() const noexcept -> bool { return !undo_stack_.empty(); }
    [[nodiscard]] auto can_redo() const noexcept -> bool { return !redo_stack_.empty(); }

    [[nodiscard]] auto undo_size() const noexcept -> std::size_t { return undo_stack_.size(); }
    [[nodiscard]] auto redo_size() const noexcept -> std::size_t { return redo_stack_.size(); }

    // Peek at the top of either stack without popping. Returns nullptr
    // when empty. Useful for UI ("Undo: 'Add block'").
    [[nodiscard]] auto peek_undo() const noexcept -> const Command*;
    [[nodiscard]] auto peek_redo() const noexcept -> const Command*;

    [[nodiscard]] auto max_depth() const noexcept -> std::size_t { return max_depth_; }

private:
    std::vector<std::unique_ptr<Command>> undo_stack_;
    std::vector<std::unique_ptr<Command>> redo_stack_;
    std::size_t max_depth_{kDefaultMaxDepth};
};

}  // namespace noted::domain
