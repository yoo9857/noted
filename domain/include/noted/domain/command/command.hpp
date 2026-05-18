#pragma once

// Command — single, reversible state mutation on a `Document`.
//
// Every user-visible edit (add/remove/move/set_payload/set_visible/
// set_name/insert) is encoded as a concrete `Command` subclass. The
// `UndoStack` (see undo_stack.hpp) owns the stack of applied commands;
// `undo()` calls `Command::undo()` to rewind, `redo()` re-applies.
//
// Why pass `Document&` to apply/undo instead of holding a Document
// pointer in the command:
//   - Commands have to replay on a **different** `Document` instance —
//     CRDT replication ships them across machines, the file format
//     replays them on load to rebuild history, undo stacks survive
//     across document reopens. A bound pointer would dangle on every
//     one of those cases.
//   - Tests construct a fresh Document per case and run commands
//     against it. Zero plumbing.
//
// Atomicity contract:
//   - `apply()` must leave the document either fully mutated (success)
//     OR untouched (any Result::error). The undo stack only retains
//     commands that returned success.
//   - `undo()` must be the precise inverse of a successful `apply()`
//     **on the same Document state** — i.e. apply(d) → undo(d) returns
//     d to its prior state.
//
// Concrete commands are declared in commands.hpp (one header that
// includes them all). This base file stays minimal so consumers that
// only need the abstract interface can include it cheaply.
//
// Rationale: see docs/architecture/0024-command-undo-redo.md.

#include <string>

#include "noted/engine/error/error.hpp"

namespace noted::domain {

class Document;  // forward — full include only needed by concrete commands

class Command {
public:
    Command() = default;
    Command(const Command&) = delete;
    auto operator=(const Command&) -> Command& = delete;
    Command(Command&&) = delete;
    auto operator=(Command&&) -> Command& = delete;
    virtual ~Command() = default;

    // Apply the change. On success, internal state is captured so a
    // subsequent `undo(doc)` can reverse it. On failure, the document
    // and the command itself are untouched.
    [[nodiscard]] virtual auto apply(Document& doc) -> Result<void> = 0;

    // Reverse a previous successful `apply(doc)`. Must be called on a
    // document that is in the post-apply state. Returns an error only
    // for internal inconsistencies (e.g. corrupted document, bug); a
    // well-formed command on a well-formed document never fails.
    [[nodiscard]] virtual auto undo(Document& doc) -> Result<void> = 0;

    // Short human-readable label, e.g. "Add block", "Move block".
    // Used by the UI's undo menu. Stable across versions.
    [[nodiscard]] virtual auto label() const noexcept -> std::string_view = 0;
};

}  // namespace noted::domain
